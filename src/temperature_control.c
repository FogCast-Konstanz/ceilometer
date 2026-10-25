#include "temperature_control.h"
#include <math.h>
bool temperature_fresh(temperature_sample s, uint32_t now) {
    return s.valid && (uint32_t)(now-s.sampled_us) <= TEMPERATURE_MAX_AGE_US &&
           s.mc >= -55000 && s.mc <= 125000;
}
bool temperature_laser_allowed(temperature_sample s, uint32_t now) {
    return temperature_fresh(s,now) && s.mc >= 10000 && s.mc <= 30000;
}
bool temperature_tec_allowed(temperature_sample s, uint32_t now) {
    return temperature_fresh(s,now) && s.mc >= -10000 && s.mc <= 50000;
}
void temperature_init(temperature_controller *c) {
    *c = (temperature_controller){.target_mc=24000};
    /* Same laphtec algorithm, plant-specific commissioning gains in mV/C.
       No laser-current feedforward: there is no calibrated current yet. */
    pid_init(&c->pid,100.0f,10.0f,0.0f,-500,500,-500,500);
}
void temperature_stop(temperature_controller *c) {
    c->enabled=false; c->output_mv=0; pid_reset(&c->pid);
}
bool temperature_start(temperature_controller *c, temperature_sample s, uint32_t now) {
    if (c->fault || (c->polarity != 1 && c->polarity != -1) || !temperature_tec_allowed(s,now)) return false;
    pid_reset(&c->pid); c->enabled=true; c->output_mv=0;
    c->last_sequence=s.sequence-1u;
    c->last_sample_us=s.sampled_us-1000000u;
    return true;
}
bool temperature_update(temperature_controller *c, temperature_sample s, uint32_t now) {
    if (!c->enabled) return !c->fault;
    if (!temperature_tec_allowed(s,now)) goto fail;
    if (s.sequence == c->last_sequence) return true; /* Never integrate cached readings. */
    uint32_t elapsed=s.sampled_us-c->last_sample_us;
    if (elapsed < 10000u || elapsed > TEMPERATURE_MAX_AGE_US) goto fail;
    float out=pid_calculate(&c->pid,c->target_mc/1000.0f,s.mc/1000.0f,elapsed/1000000.0f,0);
    if (!isfinite(out)) goto fail;
    c->output_mv=(int)(out*c->polarity);
    c->last_sample_us=s.sampled_us; c->last_sequence=s.sequence;
    return true;
fail:
    temperature_stop(c); c->fault=true; return false;
}
bool ds18b20_decode(const uint8_t p[9], int32_t *mc) {
    uint8_t crc=0;
    for (unsigned i=0;i<9;++i) {
        uint8_t v=p[i];
        for (unsigned b=0;b<8;++b,v>>=1) {
            unsigned mix=(crc^v)&1; crc>>=1; if (mix) crc^=0x8c;
        }
    }
    if (crc || p[5]!=0xff || p[7]!=0x10 || (p[4]&0x9f)!=0x1f) return false;
    int16_t raw=(int16_t)((uint16_t)p[1]<<8|p[0]);
    /* Mask undefined bits at reduced resolutions; reject power-on 85 C sentinel. */
    unsigned resolution=(p[4]>>5)&3u;
    raw=(int16_t)((uint16_t)raw & (uint16_t)~((1u<<(3u-resolution))-1u));
    if (raw < -880 || raw > 2000 || raw == 1360) return false;
    *mc=(int32_t)raw*1000/16; return true;
}
