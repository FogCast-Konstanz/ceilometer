/* Run the actual one-wire sensor driver against scripted GPIO read slots. */
#include <stdio.h>
#include <stdlib.h>
#include "sensors.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static uint32_t now;
static bool bits[256];
static unsigned used,read_at;
i2c_inst_t *i2c1;
void gpio_init(uint pin) { (void)pin; }
void gpio_put(uint pin,bool value) { (void)pin; (void)value; }
void gpio_set_dir(uint pin,bool output) { (void)pin; (void)output; }
bool gpio_get(uint pin) { (void)pin; CHECK(read_at<used); return bits[read_at++]; }
void busy_wait_us_32(uint32_t us) { now+=us; }
uint32_t time_us_32(void) { return now; }
uint32_t save_and_disable_interrupts(void) { return 0; }
void restore_interrupts(uint32_t irq) { (void)irq; }
int i2c_write_timeout_us(i2c_inst_t *b,uint8_t a,const uint8_t *p,size_t n,bool s,uint32_t t) {
    (void)b;(void)a;(void)p;(void)n;(void)s;(void)t;return -1;
}
int i2c_read_timeout_us(i2c_inst_t *b,uint8_t a,uint8_t *p,size_t n,bool s,uint32_t t) {
    (void)b;(void)a;(void)p;(void)n;(void)s;(void)t;return -1;
}
static void bit(bool v) { CHECK(used<256); bits[used++]=v; }
static void byte(uint8_t v) { for(unsigned b=0;b<8;++b) bit((v>>b)&1); }
static void reset(void) { now=0;used=read_at=0;sensors_init(); }
static void start(void) { now=1000000;bit(false);sensors_poll();CHECK(read_at==used); }
static void complete(bool corrupt) {
    now+=750000; byte(255); bit(false);
    uint8_t p[]={0x91,0x01,0x4b,0x46,0x7f,0xff,0x0c,0x10,0x70};
    if(corrupt) p[8]^=1;
    for(unsigned i=0;i<9;++i) byte(p[i]);
    sensors_poll(); CHECK(read_at==used);
}
int main(void) {
    int32_t mc;
    reset(); CHECK(!sensors_laser_mc(&mc)); start(); CHECK(!sensors_laser_mc(&mc));
    complete(false); CHECK(sensors_laser_mc(&mc) && mc==25062);
    temperature_sample previous=sensors_laser_sample();
    now=2100000;bit(false);sensors_poll();
    CHECK(sensors_laser_sample().sampled_us==previous.sampled_us);
    CHECK(sensors_laser_sample().sequence==previous.sequence);
    now=previous.sampled_us+TEMPERATURE_MAX_AGE_US+1;
    CHECK(!sensors_laser_mc(&mc)); /* pending conversion cannot keep old data fresh */
    reset(); start(); complete(true); CHECK(!sensors_laser_mc(&mc));
    reset(); now=1000000;bit(true);sensors_poll();CHECK(!sensors_laser_mc(&mc));
    reset(); start(); now+=750000;byte(0);sensors_poll();
    CHECK(!sensors_laser_mc(&mc) && read_at==used); /* conversion not complete */
    reset();start();complete(false);CHECK(sensors_laser_mc(&mc));
    now=2100000;bit(true);sensors_poll();CHECK(!sensors_laser_mc(&mc)); /* disconnect invalidates old data */
    puts("PASS: actual sensor driver conversion/presence/CRC/disconnect paths and pending-conversion freshness regression");
    return 0;
}
