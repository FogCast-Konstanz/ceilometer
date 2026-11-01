#include "integration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint8_t code[PRN_MAX_LENGTH];
static void sequence_test(unsigned n) {
    CHECK(prn_generate_length(code,n,1));
    unsigned ones=0;
    for(unsigned i=0;i<n;++i) ones+=code[i];
    CHECK(ones==(n+1)/2);
    for(unsigned lag=0;lag<n;++lag) {
        int dot=0;
        for(unsigned i=0;i<n;++i) dot+=(code[i]?1:-1)*(code[(i+lag)%n]?1:-1);
        CHECK(dot==(lag ? -1 : (int)n));
    }
    static uint32_t packed[PRN_MAX_LENGTH];prn_pack_length(code,n,packed);
    for(unsigned i=0;i<n*32u;++i) CHECK(((packed[i/32]>>(i%32))&1)==code[i%n]);
}
int main(void) {
    sequence_test(1023);sequence_test(4095);
    CHECK(!prn_generate_length(code,2047,1));CHECK(!prn_generate_length(code,4095,0));
    CHECK(!integration_init(0,10000000,1000000,2));
    CHECK(integration_init(1023,10000000,1000000,2));
    uint32_t counter=65530;CHECK(integration_push(counter));
    for(unsigned i=0;i<2*1023u;++i) CHECK(integration_push(counter+=60000));
    for(unsigned i=0;i<9775u*1023u;++i) CHECK(integration_push(counter+=3));
    CHECK(!integration_pending());integration_commit(true);
    const integration_result *v=integration_pending();CHECK(v && v->frames==9775);
    CHECK(v->first_chip==2046 && v->end_chip==2046+9775u*1023u && v->exposure_ns==999982500);
    for(unsigned i=0;i<1023;++i) CHECK(integration_histogram()[i]==9775u*3u);
    uint32_t id=v->id;CHECK(!integration_release(id+1));
    /* A held result remains immutable; next profile is explicitly dropped. */
    for(unsigned i=0;i<9775u*1023u;++i) CHECK(integration_push(counter+=4));
    integration_commit(true);CHECK(integration_dropped()==1 && v->id==id);
    CHECK(integration_histogram()[0]==9775u*3u);CHECK(integration_release(id));
    /* A timing-invalid block must never publish its staged profile. */
    CHECK(integration_init(4095,10000000,100000,1));counter=0;CHECK(integration_push(counter));
    for(unsigned i=0;i<(1+244u)*4095u;++i) CHECK(integration_push(++counter));
    integration_commit(false);CHECK(!integration_pending());
    CHECK(integration_init(4095,10000000,100000,1));
    uint32_t chunk[4096];counter=65530;
    unsigned samples=1+(1+244u)*4095u;
    for(unsigned pos=0;pos<samples;) {
        unsigned count=samples-pos;if(count>4096) count=4096;
        for(unsigned i=0;i<count;++i) {chunk[i]=counter;counter+=17;}
        CHECK(integration_block(chunk,count));integration_commit(true);pos+=count;
    }
    v=integration_pending();CHECK(v && v->length==4095 && v->frames==244);
    for(unsigned i=0;i<4095;++i) CHECK(integration_histogram()[i]==244u*17u);
    CHECK(integration_release(v->id));
    CHECK(integration_init(1023,10000,1000000,1));counter=0;CHECK(integration_push(counter));
    uint64_t valid_frames=0;unsigned profiles=0;
    for(unsigned frame=0;profiles<20;++frame) {
        for(unsigned i=0;i<1023;++i) CHECK(integration_push(++counter));
        integration_commit(true);v=integration_pending();
        if(v) {
            ++profiles;valid_frames+=v->frames;
            CHECK(valid_frames==(uint64_t)profiles*10000u/1023u);
            CHECK(integration_release(v->id));
        }
    }
    puts("PASS: both maximal sequences, exact packing, rollover, startup discard, exposure, ownership, drops, invalid-block publication");
}
