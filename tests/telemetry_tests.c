/* Actual ADC/DAC/LT8722 drivers and telemetry scanner, with I2C/SPI/GPIO buses simulated. */
#include "telemetry.h"
#include "parameters.h"
#include "board.h"
#include "config.h"
#include "sensors.h"
#include "lt8722.h"
#include "lt8722_codec.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/structs/adc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
static uint32_t now=1000000,regs[8],bus_calls,scanned;
static uint16_t dac[3],analog[5]={1000,1500,500,2276,964};
static uint16_t mux[13]={2398,2130,1700,1685,2236,2048,1707,2253,2100,2219,1857,1802,1767};
static bool pin_value[32],pin_output[32],spi_fail,i2c_fail,adc_fail,tec_fault;
static unsigned selected_adc;
static i2c_inst_t bus0,bus1;
i2c_inst_t *i2c0=&bus0,*i2c1=&bus1;
static spi_inst_t spi;
spi_inst_t *spi1=&spi;
static fake_adc_hw adc;
fake_adc_hw *adc_hw=&adc;
static temperature_sample ld={.valid=true,.mc=24000,.sampled_us=1000000,.sequence=1};
void gpio_init(unsigned p) { (void)p; }
void gpio_put(unsigned p,bool v) { pin_value[p]=v; }
void gpio_set_dir(unsigned p,bool out) { pin_output[p]=out; }
bool gpio_get(unsigned p) { return p==PIN_TEC_SWEN && !pin_output[p] ? !tec_fault : pin_value[p]; }
void gpio_set_function(unsigned p,int f) { (void)p;(void)f; }
void gpio_pull_up(unsigned p) { pin_value[p]=true; }
uint32_t time_us_32(void) { return now; }
void busy_wait_us_32(uint32_t us) { now+=us; }
void sleep_us(uint32_t us) { now+=us; }
void sleep_ms(uint32_t ms) { now+=ms*1000; }
void i2c_init(i2c_inst_t *b,unsigned rate) { (void)b;(void)rate; }
void adc_init(void) {}
void adc_gpio_init(unsigned p) { (void)p; }
void adc_set_temp_sensor_enabled(bool b) { (void)b; }
void adc_select_input(unsigned i) { selected_adc=i; }
uint16_t adc_read(void) {
    adc.cs=adc_fail ? ADC_CS_ERR_BITS : 0;
    if(selected_adc==1 && (regs[7]&64) && (regs[7]&15)<13) return mux[regs[7]&15];
    return analog[selected_adc];
}
int i2c_write_timeout_us(i2c_inst_t *b,uint8_t a,const uint8_t *p,size_t n,bool stop,uint32_t t) {
    (void)stop;(void)t;++bus_calls;if(i2c_fail) return -1;
    unsigned id=b==i2c1 ? 2 : a==I2C_LD_ADDRESS ? 0 : 1;
    if(n==3 && p[0]==0x30) dac[id]=(uint16_t)((uint16_t)p[1]<<8|p[2]);
    return (int)n;
}
int i2c_read_timeout_us(i2c_inst_t *b,uint8_t a,uint8_t *p,size_t n,bool stop,uint32_t t) {
    (void)stop;(void)t;++bus_calls;if(i2c_fail || n!=2) return -1;
    unsigned id=b==i2c1 ? 2 : a==I2C_LD_ADDRESS ? 0 : 1;
    p[0]=(uint8_t)(dac[id]>>8);p[1]=(uint8_t)dac[id];return 2;
}
void spi_init(spi_inst_t *s,unsigned rate) { (void)s;(void)rate; }
void spi_set_format(spi_inst_t *s,unsigned b,int p,int c,int order) { (void)s;(void)b;(void)p;(void)c;(void)order; }
int spi_write_read_blocking(spi_inst_t *s,const uint8_t *tx,uint8_t *rx,size_t n) {
    (void)s;CHECK(n==8);++bus_calls;if(spi_fail) return -1;
    memset(rx,0,8);unsigned reg=tx[1]>>1;CHECK(reg<8);
    rx[0]=(uint8_t)(regs[1]>>8);rx[1]=(uint8_t)regs[1];rx[7]=0xa5;
    if(tx[0]==0xf2) {
        CHECK(tx[6]==lt8722_crc(tx,6));
        regs[reg]=(uint32_t)tx[2]<<24|(uint32_t)tx[3]<<16|(uint32_t)tx[4]<<8|tx[5];
        if(reg==7 && (regs[reg]&64)) scanned|=1u<<(regs[reg]&15);
        rx[2]=lt8722_crc(rx,2);
    } else {
        CHECK(tx[0]==0xf4 && tx[2]==lt8722_crc(tx,2));
        for(unsigned i=0;i<4;++i) rx[2+i]=(uint8_t)(regs[reg]>>(24-8*i));
        rx[6]=lt8722_crc(rx,6);
    }
    return 8;
}
temperature_sample sensors_laser_sample(void) { temperature_sample s=ld;s.mc+=PARAM(laser_temp_offset_mc);return s; }
temperature_sample sensors_laser_uncalibrated(void) { return ld; }
const uint8_t *sensors_scratchpad(void) { static const uint8_t p[9]={0x80,1,0x4b,0x46,0x7f,0xff,0x0c,0x10,0};return p; }
void sensors_errors(uint32_t *p,uint32_t *c,uint32_t *v) { *p=2;*c=3;*v=4; }
bool sensors_receiver_configure(void) { return !i2c_fail; }
bool sensors_receiver_register(unsigned r,uint16_t *v) { *v=r==0 ? 25*256 : r==1 ? 0x60 : 30*256;return !i2c_fail; }
uint16_t sipm_counter_read(void) { return 12345; }
static telemetry_value get(unsigned id) { telemetry_value v;CHECK(telemetry_read(id,now,&v));return v; }
int main(void) {
    parameters_defaults(&parameters_current);board_init();lt8722_init();telemetry_init();
    regs[0]=0x08a214;regs[7]=0;telemetry_poll(false);
    CHECK(regs[0]==0x08a214 && !get(T_lt_mux0_raw).valid); /* telemetry never enables the converter */
    CHECK(get(T_vsys_uv).valid && get(T_vsys_uv).value>4900000);
    CHECK(get(T_receiver_temp_mc).valid && get(T_receiver_temp_mc).value==25000);
    CHECK(!get(T_laser_current_ma).valid && get(T_laser_adc_raw).value==1000);
    for(unsigned i=0;i<TELEMETRY_COUNT;++i) CHECK(telemetry_find(telemetry_descriptors[i].name)==(int)i);
    uint16_t readback;
    CHECK(board_dac(0,12345) && board_dac_read(0,&readback) && readback==12345);
    CHECK(board_dac_requested(0)==12345 && !board_dac_read(3,&readback));
    PARAM(laser_sense_uv_per_ma)=100;PARAM(bias_sense_uv_per_ua)=1000;
    PARAM(threshold_code)=3456;
    CHECK(lt8722_enable());CHECK(!pin_output[PIN_TEC_SWEN]); /* released, never a driven high */
    now+=100000;telemetry_poll(false);
    CHECK(dac[2]==3456 && get(T_dac2_readback).value==3456); /* receiver reconnect/application path */
    CHECK(scanned==0x1fff && regs[7]==0); /* every distinct monitor source and restore */
    CHECK(get(T_lt_output_uv).valid && get(T_lt_output_uv).value>200000 && get(T_lt_output_uv).value<300000);
    CHECK(get(T_lt_output_ua).valid && get(T_lt_output_ua).value>80000 && get(T_lt_output_ua).value<120000);
    CHECK(get(T_lt_vin_uv).valid && get(T_lt_vin_uv).value>4900000 && get(T_lt_vin_uv).value<5100000);
    CHECK(get(T_laser_current_ma).valid && get(T_dac0_readback).value==12345);
    uint32_t calls=bus_calls;telemetry_poll(true);CHECK(bus_calls==calls);
    now+=600000;CHECK(!get(T_vsys_uv).valid); /* freshness expires without new successful reads */
    CHECK(bus_calls==calls);
    telemetry_reset_active();telemetry_capture_active(true);
    CHECK(get(T_laser_active_raw).valid && get(T_laser_active_samples).value==1);
    CHECK(get(T_laser_active_current_ma).valid);
    adc_fail=true;telemetry_capture_active(true);CHECK(!get(T_laser_active_raw).valid);adc_fail=false;
    spi_fail=true;i2c_fail=true;now+=100000;telemetry_poll(false);
    CHECK(!get(T_dac0_readback).valid && !get(T_lt_register1).valid && !get(T_lt_output_uv).valid);
    CHECK(!get(T_receiver_temp_mc).valid && lt8722_error_count()>0);
    spi_fail=false;i2c_fail=false;tec_fault=true;CHECK(!lt8722_healthy());tec_fault=false;
    lt8722_disable();CHECK(pin_output[PIN_TEC_SWEN] && !pin_value[PIN_TEC_SWEN]);
    puts("PASS: all telemetry sources, real SPI/DAC/ADC framing, AMUX scan/restore, derived references, cached/no-bus readback, stale/failure propagation and shared SWEN");
    return 0;
}
