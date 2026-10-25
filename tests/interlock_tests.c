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
    temperature_init(&thermal);
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
    puts("PASS: real USB command/interlock wiring, TEC sensor prerequisite, laser 10/30 C gates, range recovery, stale feedback, latched faults and SPI shutdown");
    return 0;
}
