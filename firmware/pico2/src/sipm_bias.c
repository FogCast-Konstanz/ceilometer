#include "sipm_bias.h"
#include "parameters.h"
#include "board.h"
#include "config.h"
#include "sensors.h"
#include "pico/stdlib.h"
#include <string.h>
static bias_status state;
static temperature_sample feedback;
static uint32_t last_poll,normal_since;
static bool polled,normal,settled,test_mode;
static int32_t test_mc;
bool sipm_bias_test_active(void) { return test_mode; }
bool sipm_bias_test_temperature(int32_t mc) {
    if(state.enabled || mc < -40000 || mc > 85000) return false;
    test_mc=mc;test_mode=true;return true;
}
/* R14+R17=1208k, R21=20k; R22=15.4k, R23=10k.
   MONIN = DAC * (614/10) * (100/254); APD = MONIN - 5V.
   Gain calibration is relative to this exact resistor-ratio model. */
static int64_t span_uv(void) {
    return (int64_t)PARAM(bias_dac_ref_uv)*6140*PARAM(bias_gain_ppm)/(254*1000000LL);
}
bool sipm_bias_calculate(temperature_sample s,uint32_t now,bias_status *out) {
    *out=(bias_status){.temperature=s,.reason=BIAS_TEMPERATURE};
    if(!s.valid || s.mc < -40000 || s.mc > 85000 ||
       now-s.sampled_us > (uint32_t)PARAM(bias_temperature_age_ms)*1000u) return false;
    out->breakdown_uv=(int64_t)PARAM(sipm_breakdown_mv)*1000+
        (int64_t)PARAM(sipm_tempco_uv_c)*(s.mc-PARAM(sipm_reference_mc))/1000;
    out->target_uv=out->breakdown_uv+(int64_t)PARAM(sipm_overvoltage_mv)*1000;
    int64_t limit=(int64_t)PARAM(bias_limit_mv)*1000;
    if(limit>50000000) limit=50000000; /* Immutable ceiling even if RAM configuration is corrupt. */
    out->reason=BIAS_VOLTAGE_LIMIT;
    if(out->target_uv<=0 || out->target_uv>limit) return false;
    int64_t span=span_uv(),v=out->target_uv-PARAM(bias_offset_uv);
    if(span<=0 || v<=0) return false;
    /* Floor the DAC code: quantization can never increase the requested voltage. */
    int64_t code=v*65536/span;
    out->reason=BIAS_BUILD_LIMIT;
    if(code<=0 || code>65535 || code>CEIL_BIAS_MAX_CODE) return false;
    out->code=(uint16_t)code;
    out->estimated_uv=span*code/65536+PARAM(bias_offset_uv);
    if(out->estimated_uv>limit) { out->reason=BIAS_VOLTAGE_LIMIT;return false; }
    out->valid=true;out->reason=BIAS_OK;return true;
}
void sipm_bias_stop(void) {
    board_bias_enable(false);state.enabled=false;
    if(test_mode) feedback.valid=false;
    test_mode=false;
    (void)board_dac(1,0);
}
void sipm_bias_init(void) {
    memset(&state,0,sizeof state);feedback=(temperature_sample){0};
    test_mode=false;test_mc=0;polled=false;normal=false;settled=false;last_poll=normal_since=0;
    state.reason=BIAS_TEMPERATURE;
}
void sipm_bias_reconfigure(void) {
    bool fault=state.fault;bias_reason reason=state.reason;
    sipm_bias_init();state.fault=fault;
    if(fault) state.reason=reason;
}
static void poll_temperature(uint32_t now) {
    if(polled && now-last_poll<100000u) return;
    polled=true;last_poll=now;
    uint16_t cfg;
    bool ok=sensors_receiver_register(1,&cfg);
    uint16_t wanted=(uint16_t)((PARAM(receiver_resolution)-9)<<5);
    if(!ok || (cfg&0x61u)!=wanted) {
        normal=false;feedback.valid=false;
        if(ok) (void)sensors_receiver_configure();
        return;
    }
    if(!normal) { normal=true;settled=false;normal_since=now; }
    /* Allow a complete conversion after power-up/reconfiguration. At 12 bits
       MCP9800 maximum conversion time is 600ms (240ms typical). */
    int32_t mc;
    if(!sensors_receiver_mc(&mc)) { feedback.valid=false;normal=false;return; }
    if(!settled) {
        if(now-normal_since<600000u) { feedback.valid=false;return; }
        settled=true; /* Do not repeat warmup at the 32-bit clock rollover. */
    }
    feedback=(temperature_sample){.valid=true,.mc=mc,.sampled_us=now-600000u,
                                  .sequence=feedback.sequence+1u};
}
bool sipm_bias_service(bool available) {
    uint32_t now=time_us_32();
    uint32_t previous=feedback.sequence;
    if(test_mode) {
        feedback=(temperature_sample){.valid=true,.mc=test_mc,.sampled_us=now,
            .sequence=feedback.sequence+(available ? 1u : 0u)};
    } else if(available) poll_temperature(now);
    bias_status next;bool ok=sipm_bias_calculate(feedback,time_us_32(),&next);
    next.enabled=state.enabled;next.fault=state.fault;
    if(state.fault) { next.reason=state.reason;state=next;return false; }
    if(state.enabled && !ok) {
        state=next;sipm_bias_stop();state.fault=true;return false;
    }
    if(state.enabled && available && feedback.sequence!=previous) {
        uint16_t actual;
        if((next.code!=state.code && !board_dac(1,next.code)) ||
           !board_dac_read(1,&actual) || actual!=next.code) {
            state=next;sipm_bias_stop();state.fault=true;state.reason=BIAS_DAC_FAILURE;return false;
        }
    }
    state=next;return ok;
}
bool sipm_bias_enable(void) {
    if(state.fault || !CEIL_ENABLE_BIAS || !CEIL_BIAS_MAX_CODE) return false;
    if(!sipm_bias_service(true)) return false;
    if(state.enabled) return true;
    board_bias_enable(false);
    uint16_t actual;
    if(!board_dac(1,state.code) || !board_dac_read(1,&actual) || actual!=state.code) {
        sipm_bias_stop();state.fault=true;state.reason=BIAS_DAC_FAILURE;return false;
    }
    board_bias_enable(true);state.enabled=true;return true;
}
bool sipm_bias_clear_fault(void) {
    if(state.enabled) return false;
    bias_status check;
    if(state.fault && !sipm_bias_calculate(feedback,time_us_32(),&check)) return false;
    state.fault=false;return true;
}
bias_status sipm_bias_status(void) { return state; }
