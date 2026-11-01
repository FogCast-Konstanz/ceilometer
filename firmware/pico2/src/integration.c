#include "integration.h"
#include <stdatomic.h>
#include <string.h>
#include <limits.h>
static uint64_t active[PRN_MAX_LENGTH],result_counts[PRN_MAX_LENGTH];
static integration_result result;
static atomic_bool published;
static atomic_uint dropped;
static unsigned length,bin,frames,discard;
static uint32_t hz,period,id,target;
static uint64_t chip,first,residual;
static uint16_t last;
static bool baseline,staged;
static uint32_t next_target(void) {
    residual+=(uint64_t)hz*period;
    uint32_t n=(uint32_t)(residual/((uint64_t)length*1000000u));
    residual%=((uint64_t)length*1000000u);
    return n;
}
bool integration_init(unsigned n,uint32_t h,uint32_t p,unsigned skip) {
    if((n!=1023 && n!=4095) || !h || !skip || (uint64_t)h*p<(uint64_t)n*1000000u) return false;
    length=n;hz=h;period=p;discard=skip;bin=frames=0;chip=first=residual=0;baseline=false;staged=false;
    memset(active,0,sizeof active);atomic_store(&published,false);atomic_store(&dropped,0);
    target=next_target(); /* IDs deliberately continue across stream restarts. */
    return true;
}
bool integration_restart_burst(unsigned skip) {
    if(bin || !skip) return false;
    baseline=false;discard=skip;return true;
}
unsigned integration_frames_remaining(void) {return target-frames;}
static void finish_frame(void) {
    bin=0;
    if(discard) {--discard;if(!frames) first=chip;return;}
    if(++frames<target) return;
    ++id;
    if(!staged && !atomic_load_explicit(&published,memory_order_acquire)) {
        memcpy(result_counts,active,length*sizeof *active);
        result=(integration_result){id,length,frames,hz,period,first,chip,
            (uint64_t)frames*length*1000000000ull/hz};
        staged=true;
    } else atomic_store(&dropped,atomic_load(&dropped)+1u);
    memset(active,0,length*sizeof *active);frames=0;first=chip;target=next_target();
}
bool integration_push(uint32_t sample) {
    if(length!=1023 && length!=4095) return false;
    uint16_t current=(uint16_t)sample;
    if(!baseline) {last=current;baseline=true;return true;}
    uint16_t count=(uint16_t)(current-last);last=current;++chip;
    if(!discard) {
        if(active[bin]>UINT64_MAX-count) return false;
        active[bin]+=count;
    }
    if(++bin==length) finish_frame();
    return true;
}
bool integration_block(const volatile uint32_t *samples,unsigned count) {
    if(length!=1023 && length!=4095) return false;
    unsigned pos=0;
    if(!baseline && count) {last=(uint16_t)samples[pos++];baseline=true;}
    while(pos<count) {
        unsigned n=length-bin;if(n>count-pos) n=count-pos;
        uint16_t previous=last;
        if(!discard) {
            /* Process a contiguous PRN segment without per-snapshot global state
               updates or function calls. SRAM/DMA contention still needs measurement. */
            for(unsigned i=0;i<n;++i) {
                uint16_t current=(uint16_t)samples[pos+i];
                uint16_t delta=(uint16_t)(current-previous);previous=current;
                if(active[bin+i]>UINT64_MAX-delta) return false;
                active[bin+i]+=delta;
            }
        } else previous=(uint16_t)samples[pos+n-1];
        last=previous;pos+=n;bin+=n;chip+=n;
        if(bin==length) finish_frame();
    }
    return true;
}
void integration_cancel(void) {atomic_store(&published,false);staged=false;}
void integration_commit(bool valid) {
    if(staged && valid) atomic_store_explicit(&published,true,memory_order_release);
    staged=false;
}
const integration_result *integration_pending(void) {
    return atomic_load_explicit(&published,memory_order_acquire) ? &result : 0;
}
const uint64_t *integration_histogram(void) {return result_counts;}
bool integration_release(uint32_t requested) {
    if(!integration_pending() || result.id!=requested) return false;
    atomic_store_explicit(&published,false,memory_order_release);return true;
}
uint32_t integration_dropped(void) {return atomic_load(&dropped);}
bool prn_generate_length(uint8_t *code,unsigned n,uint16_t seed) {
    if((n!=1023 && n!=4095) || !seed || seed>n) return false;
    unsigned bits=n==1023 ? 10 : 12, tap=n==1023 ? 3 : 1;
    uint16_t state=seed;
    for(unsigned i=0;i<n;++i) {
        code[i]=state&1u;
        state=(state>>1)|(((n==1023 ? state^(state>>tap) : state^(state>>1)^(state>>4)^(state>>6))&1u)<<(bits-1));
    }
    return state==seed;
}
void prn_pack_length(const uint8_t *code,unsigned n,uint32_t *words) {
    for(unsigned w=0;w<n;++w) {
        uint32_t v=0;
        for(unsigned b=0;b<32;++b) v|=(uint32_t)code[(w*32+b)%n]<<b;
        words[w]=v;
    }
}
