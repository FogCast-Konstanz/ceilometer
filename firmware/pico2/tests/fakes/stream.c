#include "stream.h"
static stream_status state;
void stream_init(void) {}
void stream_poll(void) {}
uint64_t stream_wall_ns(void) {return 0;}
void stream_stop(void) {state.active=false;integration_cancel();}
bool stream_start(unsigned n,uint16_t seed,uint32_t hz,uint32_t p,unsigned discard,bool laser) {
    (void)seed;(void)laser;
    state.active=integration_init(n,hz,p,discard);return state.active;
}
stream_status stream_get_status(void) {return state;}

void stream_debug(uint32_t values[8]) {for(unsigned i=0;i<8;++i) values[i]=0;}
