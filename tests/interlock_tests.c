/* Exercise the real command handler and shutdown wiring with fake peripherals. */
#define main embedded_main
#include "../src/main.c"
#undef main
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static uint32_t fake_now=1000000;
static temperature_sample feedback;
static bool hw_tec, hw_gate, spi_ok=true;
static uint16_t hw_dac;
static acquisition_status acq;
void busy_wait_us_32(uint32_t us) { fake_now+=us; }
bool board_adc_sample(unsigned channel,unsigned average,uint16_t *raw) { (void)channel;(void)average;*raw=1000;return true; }
bool board_dac_read(unsigned channel,uint16_t *raw) { *raw=channel==0 ? hw_dac : 0;return true; }
uint16_t board_dac_requested(unsigned channel) { return channel==0 ? hw_dac : 0; }
bool board_pin(unsigned pin) { return pin==PIN_TEC_SWEN ? hw_tec : false; }
bool lt8722_write(uint8_t reg,uint32_t v) { (void)reg;(void)v;return spi_ok; }
uint32_t lt8722_error_count(void) { return 0; }
void prn_tx_set_code(const uint8_t p[PRN_LENGTH]) { (void)p; }
temperature_sample sensors_laser_uncalibrated(void) { return feedback; }
const uint8_t *sensors_scratchpad(void) { static uint8_t p[9];return p; }
void sensors_errors(uint32_t *p,uint32_t *c,uint32_t *v) { *p=*c=*v=0; }
bool sensors_receiver_register(unsigned r,uint16_t *v) { (void)r;*v=0;return false; }
bool sensors_receiver_configure(void) { return false; }
void gpio_init(uint pin) { (void)pin; }
void gpio_set_dir(uint pin,bool out) { (void)pin;(void)out; }
void gpio_put(uint pin,bool v) { (void)pin;(void)v; }
void gpio_set_function(uint pin,int f) { (void)pin;(void)f; }
void gpio_set_irq_enabled_with_callback(uint p,uint32_t e,bool b,void(*cb)(uint,uint32_t)) { (void)p;(void)e;(void)b;(void)cb; }
uint32_t time_us_32(void) { return fake_now; }
void stdio_init_all(void) {}
bool stdio_usb_connected(void) { return true; }
int getchar_timeout_us(uint32_t t) { (void)t;return PICO_ERROR_TIMEOUT; }
void tight_loop_contents(void) {}
bool set_sys_clock_khz(uint32_t n,bool b) { (void)n;(void)b;return true; }
uint32_t clock_get_hz(int c) { (void)c;return 150000000; }
void watchdog_enable(uint32_t n,bool b) { (void)n;(void)b; }
void watchdog_update(void) {}
void board_init(void) {}
void board_outputs_off(void) { hw_dac=0;hw_gate=false; }
bool board_dac(unsigned channel,uint16_t value) { if(!channel) hw_dac=value;return true; }
void board_bias_enable(bool b) { (void)b; }
bool board_laser_fault(void) { return false; }
uint16_t board_adc(unsigned n) { (void)n;return 0; }
void lt8722_init(void) {}
bool lt8722_enable(void) { hw_tec=spi_ok;return spi_ok; }
void lt8722_disable(void) { hw_tec=false; }
bool lt8722_set_mv(int n) { (void)n;return hw_tec && spi_ok; }
bool lt8722_read(uint8_t n,uint32_t *v) { (void)n;*v=0;return spi_ok; }
bool lt8722_healthy(void) { return spi_ok; }
void sensors_init(void) {}
void sensors_poll(void) {}
temperature_sample sensors_laser_sample(void) { return feedback; }
bool sensors_laser_mc(int32_t *v) { *v=feedback.mc;return temperature_fresh(feedback,fake_now); }
bool sensors_receiver_mc(int32_t *v) { (void)v;return false; }
void bin_acquisition_init(void) {}
void bin_acquisition_stop(void) { acq.busy=false;hw_gate=false; }
void bin_acquisition_poll(void) {}
acquisition_status bin_acquisition_status(void) { return acq; }
bool bin_acquisition_start(unsigned n,uint32_t hz,bool laser) { (void)n;(void)hz;acq.busy=true;hw_gate=laser;return true; }
const uint32_t *bin_acquisition_data(void) { static uint32_t samples[CAPTURE_MAX_SAMPLES];return samples; }
void prn_tx_init(const uint8_t p[PRN_LENGTH]) { (void)p; }
void sipm_counter_init(void) {}
uint16_t sipm_counter_read(void) { return 0; }
void test_pulse_init(void) {}
bool test_pulse_set(uint32_t n) { (void)n;return true; }
uint32_t test_pulse_actual_hz(void) { return 1000000; }
static void request(const char *name,const char *arg) {
    char cmd[64],param[64];snprintf(cmd,sizeof cmd,"%s",name);
    if(arg) snprintf(param,sizeof param,"%s",arg);
    command(cmd,arg ? param : NULL,NULL);
}
static void reset(void) {
    emergency_stop(false);fault=false;driver_fault=false;spi_ok=true;
    fake_now=1000000;last_tec_health=0;
    feedback=(temperature_sample){.valid=true,.mc=24000,.sampled_us=fake_now,.sequence=1};
    parameters_defaults(&parameters_current);parameter_storage_init();parameters_defaults(&parameters_current);
    temperature_init(&thermal);telemetry_init();
}
int main(void) {
    reset();feedback.valid=false;request("tec_polarity","-1");request("tec_enable","1");
    CHECK(!tec_on && !hw_tec);request("arm","1");CHECK(!armed);
    reset();request("tec_enable","1");CHECK(!tec_on); /* missing polarity */
    request("tec_polarity","-1");request("tec_enable","1");CHECK(tec_on && hw_tec);
    request("arm","1");request("laser_dac","1");request("start","16");
    CHECK(armed && hw_dac==1 && hw_gate && running);
    feedback.mc=30001;CHECK(thermal_service(false));
    CHECK(!armed && !hw_gate && !hw_dac && !running && tec_on && hw_tec);
    feedback.mc=24000;CHECK(thermal_service(false));CHECK(!armed); /* no auto rearm */
    request("arm","1");CHECK(armed);feedback.valid=false;
    CHECK(!thermal_service(false));CHECK(!hw_tec && !tec_on && !armed && fault && thermal.fault);
    feedback.valid=true;request("tec_enable","1");CHECK(!hw_tec);
    request("clear_fault",NULL);CHECK(!fault && !thermal.fault);
    request("tec_enable","1");CHECK(hw_tec);
    fake_now+=TEMPERATURE_MAX_AGE_US+1;CHECK(!thermal_service(false));CHECK(!hw_tec && fault);
    reset();feedback.mc=9999;request("arm","1");CHECK(!armed);
    feedback.mc=10000;request("arm","1");CHECK(armed);
    feedback.mc=30000;request("laser_dac","1");request("start","1");CHECK(hw_gate);
    feedback.mc=50001;CHECK(!thermal_service(false));CHECK(!hw_gate && !hw_dac && fault);
    reset();request("tec_polarity","1");request("tec_enable","1");CHECK(hw_tec);
    request("tec_mv","500");CHECK(thermal.output_mv==0); /* manual bypass rejected */
    spi_ok=false;fake_now+=100001;CHECK(!thermal_service(true));CHECK(!hw_tec && fault);
    reset();feedback.mc=30001;armed=true;laser_code=1;
    request("start","1");CHECK(!hw_gate && !running); /* start independently gates temperature */
    request("laser_dac","1");CHECK(!hw_dac); /* DAC path independently gated */
    reset();request("set_params","laser_min_mc,5000,laser_max_mc,35000,tec_target_mc,32000");
    CHECK(PARAM(laser_min_mc)==5000 && PARAM(laser_max_mc)==35000 && thermal.target_mc==32000);
    feedback.mc=6000;request("arm","1");CHECK(armed); /* adjustable laser window */
    request("set_param","laser_min_mc,0");CHECK(PARAM(laser_min_mc)==5000); /* no live safety edits */
    request("stop",NULL);request("set_params","laser_min_mc,36000,laser_max_mc,37000");
    CHECK(PARAM(laser_min_mc)==5000 && PARAM(laser_max_mc)==35000); /* rejected atomically */
    request("save",NULL);CHECK(parameter_storage_state()->loaded);
    request("defaults",NULL);CHECK(PARAM(laser_min_mc)==10000 && !armed && !tec_on);
    request("load",NULL);CHECK(PARAM(laser_min_mc)==5000 && thermal.target_mc==32000 && !armed && !tec_on);
    request("set_params","laser_dac_code,1,bias_dac_code,1");CHECK(hw_dac==0 && !hw_tec && !armed);
    request("save",NULL);parameter_storage_init();
    CHECK(PARAM(laser_dac_code)==1 && hw_dac==0 && !hw_tec); /* saved target never energizes boot */
    puts("PASS: real USB command/interlock wiring, TEC sensor prerequisite, laser 10/30 C gates, range recovery, stale feedback, latched faults and SPI shutdown");
    return 0;
}
