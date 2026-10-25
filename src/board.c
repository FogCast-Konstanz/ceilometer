#include "board.h"
#include "config.h"
#include "parameters.h"
#include "hardware/structs/adc.h"
static uint16_t requested_dac[3];
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
static bool dac_command(i2c_inst_t *bus, uint8_t addr, uint8_t cmd, uint16_t value) {
    uint8_t p[] = {cmd, value >> 8, value & 255};
    return i2c_write_timeout_us(bus, addr, p, 3, false, 2000) == 3;
}
bool board_dac(unsigned which, uint16_t code) {
    if (which > 2) return false;
    bool ok=dac_command(which == 2 ? i2c1 : i2c0,
        which == 0 ? I2C_LD_ADDRESS : which == 1 ? I2C_BIAS_ADDRESS : I2C_THRESHOLD_ADDRESS, 0x30, code);
    if(ok) requested_dac[which]=code;
    return ok;
}
void board_bias_enable(bool enable) { gpio_put(PIN_BIAS_ENABLE, enable); }
void board_outputs_off(void) {
    gpio_put(PIN_LD_GATE, 0); gpio_set_function(PIN_LD_GATE, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_LD_GATE, GPIO_OUT); board_bias_enable(false);
    (void)board_dac(0, 0); (void)board_dac(1, 0);
}
void board_init(void) {
    gpio_init(PIN_LD_GATE); gpio_put(PIN_LD_GATE, 0); gpio_set_dir(PIN_LD_GATE, GPIO_OUT);
    gpio_init(PIN_BIAS_ENABLE); gpio_put(PIN_BIAS_ENABLE, 0); gpio_set_dir(PIN_BIAS_ENABLE, GPIO_OUT);
    gpio_init(PIN_LD_ERROR); gpio_set_dir(PIN_LD_ERROR, GPIO_IN); gpio_pull_up(PIN_LD_ERROR);
    i2c_init(i2c0, 100000); i2c_init(i2c1, 100000);
    for (unsigned pin = 0; pin < 4; ++pin) { gpio_set_function(pin, GPIO_FUNC_I2C); gpio_pull_up(pin); }
    /* Internal 2.5 V reference, gain=1, normal operation. Do not request gain=2 on 3.3 V rails. */
    (void)dac_command(i2c0, I2C_LD_ADDRESS, 0x40, 0);
    (void)dac_command(i2c0, I2C_BIAS_ADDRESS, 0x40, 0);
    (void)dac_command(i2c1, I2C_THRESHOLD_ADDRESS, 0x40, 0);
    board_outputs_off();
    adc_init(); adc_set_temp_sensor_enabled(true);
    gpio_init(24); gpio_set_dir(24,GPIO_IN);
    for (unsigned p = 26; p <= 29; ++p) adc_gpio_init(p);
}
bool board_laser_fault(void) { return !gpio_get(PIN_LD_ERROR); }
uint16_t board_adc(unsigned channel) { adc_select_input(channel); return adc_read(); }

bool board_dac_read(unsigned which,uint16_t *code) {
    if(which>2) return false;
    uint8_t p[2];
    if(i2c_read_timeout_us(which==2 ? i2c1 : i2c0,
        which==0 ? I2C_LD_ADDRESS : which==1 ? I2C_BIAS_ADDRESS : I2C_THRESHOLD_ADDRESS,
        p,2,false,2000)!=2) return false;
    *code=(uint16_t)((uint16_t)p[0]<<8|p[1]);return true;
}
uint16_t board_dac_requested(unsigned which) { return which<3 ? requested_dac[which] : 0; }
bool board_adc_sample(unsigned channel,unsigned average,uint16_t *raw) {
    if(channel>4 || !average || average>64) return false;
    adc_select_input(channel); (void)adc_read(); /* discard after mux change */
    uint32_t sum=0;bool ok=true;
    for(unsigned i=0;i<average;++i) {
        sum+=adc_read();
        if(adc_hw->cs & ADC_CS_ERR_BITS) ok=false;
    }
    *raw=(uint16_t)((sum+average/2u)/average);return ok;
}
bool board_pin(unsigned pin) { return gpio_get(pin); }
