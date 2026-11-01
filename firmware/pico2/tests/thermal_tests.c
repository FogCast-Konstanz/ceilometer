#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "temperature_control.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static void checksum(uint8_t p[9]) {
    uint8_t crc=0;
    for (unsigned i=0;i<8;++i) {
        uint8_t v=p[i];
        for (unsigned b=0;b<8;++b) {
            unsigned mix=(crc^v)&1u; crc>>=1; if(mix) crc^=0x8c; v>>=1;
        }
    }
    p[8]=crc;
}
int main(void) {
    temperature_sample s={.valid=true,.mc=24000,.sampled_us=1000000,.sequence=1};
    temperature_controller c; temperature_init(&c);
    CHECK(!temperature_start(&c,s,1500000)); /* polarity must be commissioned */
    c.polarity=-1;
    CHECK(temperature_start(&c,s,1500000));
    CHECK(temperature_update(&c,s,1500000) && c.output_mv==0);
    s.mc=23000; s.sequence++; s.sampled_us+=1000000;
    CHECK(temperature_update(&c,s,2500000) && c.output_mv==-110);
    float integral=c.pid.integral;
    CHECK(temperature_update(&c,s,2600000) && c.pid.integral==integral);
    CHECK(!temperature_update(&c,s,4000001) && !c.enabled && c.fault && c.output_mv==0);
    s.sampled_us=4500000; s.sequence++;
    CHECK(!temperature_start(&c,s,4500000)); /* no restart merely because feedback recovers */
    c.fault=false; CHECK(temperature_start(&c,s,4500000));
    s.valid=false;
    CHECK(!temperature_update(&c,s,4500000) && c.fault && !c.enabled && c.output_mv==0);
    s.valid=true; c.fault=false;
    for (unsigned i=0;i<2;++i) {
        s.mc=i ? 30000 : 10000;
        CHECK(temperature_laser_allowed(s,4500000));
    }
    s.mc=9999; CHECK(!temperature_laser_allowed(s,4500000) && temperature_tec_allowed(s,4500000));
    s.mc=30001; CHECK(!temperature_laser_allowed(s,4500000) && temperature_tec_allowed(s,4500000));
    s.mc=50001; CHECK(!temperature_tec_allowed(s,4500000));
    s.mc=-10001; CHECK(!temperature_tec_allowed(s,4500000));
    s.mc=24000; s.valid=false; CHECK(!temperature_laser_allowed(s,4500000)); s.valid=true;
    s.sampled_us=UINT32_MAX-500000u;
    CHECK(temperature_fresh(s,500000)); CHECK(!temperature_fresh(s,1600000));
    s.sampled_us=5000000; s.sequence++;
    CHECK(temperature_start(&c,s,5500000));
    c.pid.Kp=NAN;
    CHECK(!temperature_update(&c,s,5500000) && c.fault && !c.enabled && c.output_mv==0);
    PID_Controller pid;
    pid_init(&pid,0,10,0,-500,500,-500,500);
    for(unsigned i=0;i<100;++i) CHECK(pid_calculate(&pid,100,0,1,0)<=500);
    CHECK(pid.integral==500); pid_reset(&pid); CHECK(pid.integral==0);
    pid_init(&pid,0,0,20,-500,500,-500,500);
    CHECK(pid_calculate(&pid,24,24,1,0)==0);
    CHECK(pid_calculate(&pid,25,24,1,0)==0); /* no setpoint derivative kick */
    CHECK(pid_calculate(&pid,25,25,1,0)<0);
    CHECK(isnan(pid_calculate(&pid,25,25,0,0)));
    uint8_t scratch[]={0x91,0x01,0x4b,0x46,0x7f,0xff,0x0c,0x10,0x70}; int32_t mc;
    CHECK(ds18b20_decode(scratch,&mc) && mc==25062);
    scratch[0]^=1; CHECK(!ds18b20_decode(scratch,&mc)); scratch[0]^=1;
    scratch[0]=0x50; scratch[1]=0x05; checksum(scratch);
    CHECK(!ds18b20_decode(scratch,&mc)); /* CRC-valid 85 C power-on sentinel */
    memset(scratch,0,9); CHECK(!ds18b20_decode(scratch,&mc));
    memset(scratch,0xff,9); CHECK(!ds18b20_decode(scratch,&mc));
    uint8_t lowres[]={0x97,0x01,0x4b,0x46,0x1f,0xff,0x0c,0x10,0}; checksum(lowres);
    CHECK(ds18b20_decode(lowres,&mc) && mc==25000);
    puts("PASS: temperature range boundaries, stale/disconnected feedback, latched recovery, timestamp wrap, PID saturation/reset/derivative, invalid arithmetic, DS18B20 CRC/power-on/resolution");
    return 0;
}
