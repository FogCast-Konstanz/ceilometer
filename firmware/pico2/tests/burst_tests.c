#include "stream.h"
#include "bin_acquisition.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint64_t now=1000000;
static acquisition_status acq;
static uint32_t samples[CAPTURE_MAX_SAMPLES],rate;
static unsigned n,burst,starts;
static uint16_t counter;
uint64_t time_us_64(void) {return now;}
uint32_t time_us_32(void) {return (uint32_t)now;}
void bin_acquisition_stop(void) {acq=(acquisition_status){0};}
acquisition_status bin_acquisition_status(void) {return acq;}
const uint32_t *bin_acquisition_data(void) {return samples;}
void prn_tx_set_sequence(const uint8_t *code,unsigned length) {(void)code;n=length;}
bool bin_acquisition_start_sequence(unsigned frames,unsigned length,uint32_t hz,bool laser) {
    (void)laser;CHECK(length==n);CHECK(frames*length+1<=CAPTURE_MAX_SAMPLES);
    burst=frames;rate=hz;acq=(acquisition_status){.busy=true};++starts;return true;
}
static void complete(unsigned skip) {
    /* Counter ran freely through the gap: this arbitrary jump must be ignored. */
    counter+=31001;samples[0]=counter;
    for(unsigned i=1;i<=burst*n;++i) {
        counter+=(i<=skip*n ? 50000 : 3);samples[i]=counter;
    }
    now+=((uint64_t)burst*n*1000000+rate-1)/rate;
    acq=(acquisition_status){.ready=true,.completed_us=now};
    now+=1700; /* foreground latency and sensor work after the IRQ */
}
static void profile(unsigned length,unsigned skip,unsigned expected) {
    starts=0;CHECK(stream_start(length,1,10000000,1000000,skip,false));
    unsigned accepted=0;
    while(!integration_pending()) {
        accepted+=burst-skip;complete(skip);
        unsigned before=starts;stream_poll();
        CHECK(starts==before); /* completion must yield idle to housekeeping */
        CHECK(!stream_get_status().failed);
        if(!integration_pending()) {now+=13000;stream_poll();CHECK(starts==before+1);}
    }
    const integration_result *v=integration_pending();
    CHECK(accepted==expected && v->frames==expected);
    CHECK(v->exposure_ns==(uint64_t)expected*length*1000000000/10000000);
    for(unsigned i=0;i<length;++i) CHECK(integration_histogram()[i]==3u*expected);
    CHECK(stream_wall_ns()>v->exposure_ns);
    unsigned held=starts;uint64_t wall=stream_wall_ns();
    now+=5000000;stream_poll();CHECK(starts==held && stream_wall_ns()==wall);
    CHECK(integration_histogram()[length-1]==3u*expected);
    CHECK(integration_release(v->id));stream_poll();CHECK(starts==held+1);
    stream_stop();CHECK(!stream_get_status().active && !integration_pending());
}
int main(void) {
    profile(1023,2,9775);profile(4095,2,2442);
    CHECK(!stream_start(1023,1,10000000,1000000,16,false));
    CHECK(!stream_start(4095,1,10000000,1000000,4,false));
    CHECK(stream_start(1023,1,10000000,1000000,2,false));
    complete(2);acq.failed=true;stream_poll();
    CHECK(stream_get_status().failed && !stream_get_status().active && !integration_pending());
    CHECK(stream_start(1023,1,10000000,1000000,2,false));
    stream_stop();complete(2);stream_poll();CHECK(!integration_pending());
    puts("PASS: repeated warmup, gap rollover exclusion, partial final burst, exposure, held profile, stop and DMA fault");
}
