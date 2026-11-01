#include "telemetry.h"
#include "sipm_bias.h"
#include "parameters.h"
#include "board.h"
#include "config.h"
#include "lt8722.h"
#include "sensors.h"
#include "sipm_counter.h"
#include "pico/stdlib.h"
#include <string.h>
#define DESCRIPTOR(name,unit) {#name,unit},
const telemetry_descriptor telemetry_descriptors[TELEMETRY_COUNT]={TELEMETRY_LIST(DESCRIPTOR)};
#undef DESCRIPTOR
static telemetry_value values[TELEMETRY_COUNT];
static uint32_t last_sweep,last_active,sequence,active_count;
static uint16_t active_peak;
static void store(unsigned id,int64_t value,bool valid,uint32_t stamp) {
    if(valid) values[id]=(telemetry_value){.value=value,.sampled_us=stamp,.sequence=sequence,.valid=true};
    else values[id].valid=false; /* A failed attempt never refreshes an old sample. */
}
static int64_t adc_uv(uint16_t raw) { return (int64_t)raw*PARAM(adc_reference_uv)/4096; }
void telemetry_init(void) { memset(values,0,sizeof values);last_sweep=0;sequence=0;telemetry_reset_active(); }
void telemetry_reset_active(void) {
    last_active=0;active_count=0;active_peak=0;
    values[T_laser_active_raw].valid=false;values[T_laser_active_peak_raw].valid=false;
    values[T_laser_active_samples].valid=false;
    values[T_laser_active_uv].valid=false;values[T_laser_active_current_ma].valid=false;
}
void telemetry_capture_active(bool force) {
    uint32_t now=time_us_32();
    if(!force && now-last_active<(uint32_t)PARAM(active_adc_period_us)) return;
    last_active=now;
    uint16_t raw=0;
    bool ok=board_adc_sample(0,1,&raw);
    if(ok) { ++active_count;if(raw>active_peak) active_peak=raw; }
    store(T_laser_active_raw,raw,ok,now);
    store(T_laser_active_peak_raw,active_peak,ok,now);
    store(T_laser_active_samples,active_count,ok,now);
    store(T_laser_active_uv,adc_uv(raw),ok,now);
    int64_t scale=PARAM(laser_sense_uv_per_ma);
    store(T_laser_active_current_ma,scale ? (adc_uv(raw)-PARAM(laser_sense_offset_uv))/scale : 0,ok&&scale>0,now);
}
static void derived_mux(const uint16_t raw[13],uint32_t mask,uint32_t stamp) {
    int64_t u[13];for(unsigned i=0;i<13;++i) u[i]=adc_uv(raw[i]);
#define HAVE(bits) ((mask&(bits))==(bits))
    int64_t ref25=u[5]*5/3,ref125=u[6],ref165=u[7];
    int64_t out=(ref125-u[3])*16,sfb=17*u[12]-16*ref125;
    store(T_lt_output_uv,out,HAVE((1u<<3)|(1u<<6)),stamp);
    store(T_lt_output_ua,(ref165-u[4])*8,HAVE((1u<<4)|(1u<<7)),stamp);
    store(T_lt_ref2500_uv,ref25,HAVE(1u<<5),stamp);
    store(T_lt_ref1250_uv,ref125,HAVE(1u<<6),stamp);
    store(T_lt_ref1650_uv,ref165,HAVE(1u<<7),stamp);
    store(T_lt_die_temp_mc,(u[8]-1420700)*10000/47148,HAVE(1u<<8),stamp);
    store(T_lt_vin_uv,(ref25*9/10-u[9])*8,HAVE((1u<<5)|(1u<<9)),stamp);
    store(T_lt_vcc_uv,u[10]*5/2,HAVE(1u<<10),stamp);
    store(T_lt_vddio_uv,u[11]*5/2,HAVE(1u<<11),stamp);
    store(T_lt_sfb_uv,sfb,HAVE((1u<<6)|(1u<<12)),stamp);
    store(T_lt_ldr_uv,sfb+out,HAVE((1u<<3)|(1u<<6)|(1u<<12)),stamp);
    store(T_lt_control_uv,(ref125-u[2])*5/4,HAVE((1u<<2)|(1u<<6)),stamp);
#undef HAVE
}
void telemetry_poll(bool busy) {
    if(busy) return; /* Cached readback only during acquisition; no serial bus activity. */
    uint32_t begin=time_us_32();
    if(sequence && begin-last_sweep<(uint32_t)PARAM(telemetry_period_ms)*1000u) return;
    last_sweep=begin;++sequence;
    uint16_t adc[5]={0};bool valid[5];
    for(unsigned i=0;i<5;++i) {
        valid[i]=board_adc_sample(i,(unsigned)PARAM(adc_average),&adc[i]);
        store(T_laser_adc_raw+i,adc[i],valid[i],time_us_32());
    }
    store(T_laser_sense_uv,adc_uv(adc[0]),valid[0],begin);
    store(T_bias_sense_uv,adc_uv(adc[2]),valid[2],begin);
    store(T_vsys_uv,adc_uv(adc[3])*3,valid[3],begin);
    store(T_mcu_temperature_mc,27000-(adc_uv(adc[4])-706000)*1000/1721,valid[4],begin);
    int64_t scale=PARAM(laser_sense_uv_per_ma);
    store(T_laser_current_ma,scale ? (adc_uv(adc[0])-PARAM(laser_sense_offset_uv))/scale : 0,valid[0]&&scale>0,begin);
    scale=PARAM(bias_sense_uv_per_ua);
    store(T_bias_current_ua,scale ? (adc_uv(adc[2])-PARAM(bias_sense_offset_uv))/scale : 0,valid[2]&&scale>0,begin);
    temperature_sample ld=sensors_laser_sample(),rawld=sensors_laser_uncalibrated();
    bool fresh=temperature_fresh(ld,time_us_32());
    store(T_laser_temp_mc,ld.mc,fresh,ld.sampled_us);
    store(T_laser_temp_raw_mc,rawld.mc,rawld.valid&&fresh,ld.sampled_us);
    store(T_laser_temp_sequence,ld.sequence,fresh,ld.sampled_us);
    const uint8_t *scratch=sensors_scratchpad();
    for(unsigned i=0;i<9;++i) store(T_ds_byte0+i,scratch[i],rawld.valid&&fresh,ld.sampled_us);
    /* The compensation service owns receiver configuration. A telemetry sweep
       must not silently repair a sensor reset and hide it from that service. */
    uint16_t receiver_config=0;
    bool receiver_config_ok=sensors_receiver_register(1,&receiver_config) &&
        (receiver_config&0x61u)==(uint16_t)((PARAM(receiver_resolution)-9)<<5);
    uint16_t rx=0;
    for(unsigned i=0;i<4;++i) {
        bool ok=sensors_receiver_register(i,&rx);
        int64_t value=rx;
        if(!i) value=(int32_t)(int16_t)rx*1000/256+PARAM(receiver_temp_offset_mc);
        store(T_receiver_temp_mc+i,value,ok&&receiver_config_ok,time_us_32());
    }
    store(T_ld_error,board_laser_fault(),true,begin);
    store(T_tec_swen,board_pin(PIN_TEC_SWEN),true,begin);
    store(T_bias_enabled,board_pin(PIN_BIAS_ENABLE),true,begin);
    store(T_vbus_present,board_pin(24),true,begin);
    store(T_smps_pwm,board_pin(23),true,begin);
    store(T_counter_raw,sipm_counter_read(),true,begin);
    store(T_spi_errors,lt8722_error_count(),true,begin);
    uint32_t presence,crc,conversion;sensors_errors(&presence,&crc,&conversion);
    store(T_ds_presence_errors,presence,true,begin);store(T_ds_crc_errors,crc,true,begin);
    store(T_ds_conversion_errors,conversion,true,begin);
    for(unsigned i=0;i<3;++i) {
        uint16_t code=0;bool ok=board_dac_read(i,&code);
        /* Threshold is non-power configuration; retry after a receiver reconnect.
           Never reapply laser/bias targets here: those require explicit actuation. */
        if(i==2 && (!ok || code!=(uint16_t)PARAM(threshold_code))) {
            ok=board_dac(2,(uint16_t)PARAM(threshold_code)) && board_dac_read(2,&code);
        }
        store(T_dac0_requested+i,board_dac_requested(i),true,time_us_32());
        store(T_dac0_readback+i,code,ok,time_us_32());
        int64_t ref=i==0 ? PARAM(laser_dac_ref_uv) : i==1 ? PARAM(bias_dac_ref_uv) : PARAM(threshold_dac_ref_uv);
        store(T_dac0_uv+i,(int64_t)code*ref/65536,ok,time_us_32());
    }
    uint32_t registers[8]={0};bool reg_valid[8];
    for(unsigned i=0;i<8;++i) {
        reg_valid[i]=lt8722_read((uint8_t)i,&registers[i]);
        store(T_lt_register0+i,registers[i],reg_valid[i],time_us_32());
    }
    uint16_t mux[13]={0};uint32_t mask=0;
    /* Do not enable the TEC simply to obtain telemetry. Analog blocks need ENABLE_REQ. */
    bool powered=reg_valid[0] && (registers[0]&1u);
    if(powered && reg_valid[7]) {
        for(unsigned i=0;i<13;++i) {
            bool ok=lt8722_write(7,0x40u|i);
            if(ok) {
                busy_wait_us_32((uint32_t)PARAM(amux_settle_us));
                ok=board_adc_sample(1,(unsigned)PARAM(adc_average),&mux[i]);
                /* Saturated analog monitors cannot yield a meaningful converted value. */
                if(mux[i]==0 || mux[i]>=4095) ok=false;
            }
            store(T_lt_mux0_raw+i,mux[i],ok,time_us_32());
            store(T_lt_mux0_uv+i,adc_uv(mux[i]),ok,time_us_32());
            if(ok) mask|=1u<<i;
        }
        if(!lt8722_write(7,registers[7])) { mask=0;values[T_lt_register7].valid=false; }
    } else {
        for(unsigned i=0;i<13;++i) { values[T_lt_mux0_raw+i].valid=false;values[T_lt_mux0_uv+i].valid=false; }
    }
    derived_mux(mux,mask,time_us_32());
    store(T_sweep_sequence,sequence,true,time_us_32());
    store(T_sweep_duration_us,time_us_32()-begin,true,time_us_32());
}
int telemetry_find(const char *name) {
    for(unsigned i=0;i<TELEMETRY_COUNT;++i) if(!strcmp(name,telemetry_descriptors[i].name)) return (int)i;
    return -1;
}
bool telemetry_read(unsigned index,uint32_t now,telemetry_value *out) {
    if(index>=TELEMETRY_COUNT) return false;
    if(index>=T_bias_feedback_mc) {
        bias_status b=sipm_bias_status();
        int64_t data[]={b.temperature.mc,b.breakdown_uv,b.target_uv,b.estimated_uv,
                       b.estimated_uv-b.breakdown_uv,b.code,b.fault,b.reason,b.temperature.sequence};
        bool fresh=b.temperature.valid && now-b.temperature.sampled_us<=(uint32_t)PARAM(bias_temperature_age_ms)*1000u;
        bool status=index==T_bias_fault || index==T_bias_reason;
        bool valid=status || (fresh && (index==T_bias_feedback_mc || index==T_bias_feedback_sequence ||
                    b.valid));
        *out=(telemetry_value){.value=data[index-T_bias_feedback_mc],.valid=valid,
             .sampled_us=status ? now : b.temperature.sampled_us,.sequence=b.temperature.sequence};
        return true;
    }
    *out=values[index];
    bool ds=(index>=T_laser_temp_mc && index<=T_laser_temp_sequence) || (index>=T_ds_byte0 && index<=T_ds_byte8);
    uint32_t max_age=ds ? (uint32_t)PARAM(temperature_age_ms)*1000u :
                         (uint32_t)PARAM(telemetry_period_ms)*3000u+250000u;
    if(index>=T_laser_active_raw && index<=T_laser_active_current_ma)
        max_age=UINT32_MAX; /* Last capture result, retained until the next capture. Age remains visible. */
    if(now-out->sampled_us>max_age) out->valid=false;
    return true;
}
