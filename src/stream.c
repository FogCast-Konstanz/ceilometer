#include "stream.h"
#include "prn_tx.h"
#include "bin_acquisition.h"
#include "config.h"
#include "pico/stdlib.h"
/* Compatibility stream API backed by the qualified finite-DMA architecture.
   Only core 0 runs. No RX DMA ring, background worker, or inter-burst counting. */
static uint8_t sequence[PRN_MAX_LENGTH];
static stream_status state;
static unsigned length,discard,burst_frames;
static uint32_t hz;
static bool laser,inflight;
static uint64_t profile_started,wall_ns;
void stream_init(void) {}
void stream_stop(void) {
    state.active=false;inflight=false;
    bin_acquisition_stop();integration_cancel();
}
static bool begin_burst(void) {
    unsigned capacity=(CAPTURE_MAX_SAMPLES-1u)/length;
    unsigned accepted=integration_frames_remaining();
    if(accepted>capacity-discard) accepted=capacity-discard;
    burst_frames=discard+accepted;
    if(!profile_started) profile_started=time_us_64();
    if(!integration_restart_burst(discard)) return false;
    prn_tx_set_sequence(sequence,length);
    inflight=bin_acquisition_start_sequence(burst_frames,length,hz,laser);
    return inflight;
}
bool stream_start(unsigned n,uint16_t seed,uint32_t h,uint32_t period,unsigned skip,bool emit) {
    stream_stop();state=(stream_status){0};wall_ns=profile_started=0;
    if(h<1000000 || (n!=1023 && n!=4095) || skip<1 ||
       skip>=(CAPTURE_MAX_SAMPLES-1u)/n || !prn_generate_length(sequence,n,seed) ||
       !integration_init(n,h,period,skip)) return false;
    length=n;discard=skip;hz=h;laser=emit;
    state.started_us=time_us_64();state.active=true;
    if(!begin_burst()) {stream_stop();return false;}
    return true;
}
void stream_poll(void) {
    if(!state.active) return;
    if(inflight) {
        acquisition_status s=bin_acquisition_status();
        if(s.busy) return;
        bool ok=s.ready && !s.failed;
        uint32_t t=time_us_32();
        if(ok) ok=integration_block(bin_acquisition_data(),burst_frames*length+1u);
        uint32_t elapsed=time_us_32()-t;
        if(elapsed>state.max_process_us) state.max_process_us=elapsed;
        integration_commit(ok);
        if(!ok) {
            state.failed=true;state.error_flags=s.tx_starved ? 2u : 1u;
            state.active=false;bin_acquisition_stop();integration_cancel();return;
        }
        ++state.blocks;inflight=false;
        if(integration_pending()) {
            /* IRQ timestamp excludes foreground folding and result download. */
            wall_ns=(s.completed_us-profile_started)*1000u;
            profile_started=0;
        }
        /* Return to the main loop with RX idle. Its next iteration must service
           sensors and slow telemetry before we arm another finite burst. */
        return;
    }
    /* Hold complete profiles without transmitting. Downloads and sensors run
       here; next profile begins only after explicit result release. */
    if(integration_pending()) return;
    if(!begin_burst()) {
        state.failed=true;state.error_flags=4;state.active=false;integration_cancel();
    }
}
stream_status stream_get_status(void) {return state;}
uint64_t stream_wall_ns(void) {return wall_ns;}
void stream_debug(uint32_t values[8]) {
    for(unsigned i=0;i<8;++i) values[i]=0;
    values[0]=1; /* finite single-core backend */
    values[1]=inflight;values[2]=length;values[3]=discard;
    values[4]=burst_frames;values[7]=state.blocks;
}
