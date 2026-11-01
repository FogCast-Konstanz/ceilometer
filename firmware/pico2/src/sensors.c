#include "sensors.h"
#include "config.h"
#include "parameters.h"
#include <string.h>
static uint8_t scratchpad[9];
static uint32_t presence_errors,crc_errors,conversion_errors;
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
static temperature_sample laser;
static bool pending;
static uint32_t converted_at, last_request;
static void ow_low(void) { gpio_put(PIN_ONEWIRE, 0); gpio_set_dir(PIN_ONEWIRE, GPIO_OUT); }
static void ow_release(void) { gpio_set_dir(PIN_ONEWIRE, GPIO_IN); }
static bool ow_reset(void) {
    ow_low(); busy_wait_us_32(480); ow_release();
    uint32_t irq = save_and_disable_interrupts();
    busy_wait_us_32(70); bool presence = !gpio_get(PIN_ONEWIRE);
    if(!presence) ++presence_errors;
    restore_interrupts(irq); busy_wait_us_32(410); return presence;
}
static void ow_write(uint8_t v) {
    for (unsigned b = 0; b < 8; ++b, v >>= 1) {
        uint32_t irq = save_and_disable_interrupts();
        ow_low(); busy_wait_us_32(v & 1 ? 6 : 60); ow_release();
        if (v & 1) busy_wait_us_32(54);
        restore_interrupts(irq); busy_wait_us_32(10);
    }
}
static uint8_t ow_read(void) {
    uint8_t v = 0;
    for (unsigned b = 0; b < 8; ++b) {
        uint32_t irq = save_and_disable_interrupts();
        ow_low(); busy_wait_us_32(3); ow_release(); busy_wait_us_32(10);
        if (gpio_get(PIN_ONEWIRE)) v |= 1u << b;
        busy_wait_us_32(47); restore_interrupts(irq); busy_wait_us_32(10);
    }
    return v;
}
void sensors_init(void) { laser=(temperature_sample){0}; pending=false; converted_at=0; last_request=0; gpio_init(PIN_ONEWIRE); ow_release(); }
void sensors_poll(void) {
    uint32_t now = time_us_32();
    if (pending && now - converted_at >= 750000) {
        pending = false; laser.valid = false;
        /* Externally powered device reports conversion completion in a read slot. */
        bool completed = (ow_read() & 1u) != 0;
        if(!completed) ++conversion_errors;
        if (completed && ow_reset()) {
            uint8_t p[9]; ow_write(0xcc); ow_write(0xbe);
            for (unsigned i = 0; i < 9; ++i) p[i] = ow_read();
            memcpy(scratchpad,p,sizeof p);
            if (ds18b20_decode(p, &laser.mc)) {
                laser.sampled_us = converted_at; ++laser.sequence; laser.valid = true;
            } else ++crc_errors;
        }
    }
    if (!pending && now - last_request >= (uint32_t)PARAM(ds_period_ms)*1000u) {
        last_request = now;
        if (ow_reset()) {
            ow_write(0xcc); ow_write(0x44); converted_at = time_us_32(); pending = true;
        } else laser.valid = false;
    }
}
bool sensors_laser_mc(int32_t *t) {
    temperature_sample s=sensors_laser_sample();
    if (!temperature_fresh(s, time_us_32())) return false;
    *t = s.mc; return true;
}
temperature_sample sensors_laser_sample(void) {
    temperature_sample s=laser; s.mc+=PARAM(laser_temp_offset_mc); return s;
}
temperature_sample sensors_laser_uncalibrated(void) { return laser; }
const uint8_t *sensors_scratchpad(void) { return scratchpad; }
void sensors_errors(uint32_t *presence,uint32_t *crc,uint32_t *conversion) {
    *presence=presence_errors;*crc=crc_errors;*conversion=conversion_errors;
}
bool sensors_receiver_mc(int32_t *t) {
    uint8_t reg = 0, p[2];
    if (i2c_write_timeout_us(i2c1, I2C_TEMPERATURE_ADDRESS, &reg, 1, true, 2000) != 1 ||
        i2c_read_timeout_us(i2c1, I2C_TEMPERATURE_ADDRESS, p, 2, false, 2000) != 2) return false;
    int16_t raw = (int16_t)((uint16_t)p[0] << 8 | p[1]);
    *t = (int32_t)raw * 1000 / 256 + PARAM(receiver_temp_offset_mc); return true;
}

bool sensors_receiver_register(unsigned reg,uint16_t *value) {
    if(reg>3) return false;
    uint8_t r=(uint8_t)reg,p[2];unsigned n=reg==1 ? 1 : 2;
    if(i2c_write_timeout_us(i2c1,I2C_TEMPERATURE_ADDRESS,&r,1,true,2000)!=1 ||
       i2c_read_timeout_us(i2c1,I2C_TEMPERATURE_ADDRESS,p,n,false,2000)!=(int)n) return false;
    *value=n==1 ? p[0] : (uint16_t)((uint16_t)p[0]<<8|p[1]);return true;
}
bool sensors_receiver_configure(void) {
    uint16_t config;
    if(!sensors_receiver_register(1,&config)) return false;
    uint8_t wanted=(uint8_t)((config & ~0x61u) | ((PARAM(receiver_resolution)-9)<<5));
    if(wanted==(uint8_t)config) return true;
    uint8_t p[]={1,wanted};
    return i2c_write_timeout_us(i2c1,I2C_TEMPERATURE_ADDRESS,p,2,false,2000)==2;
}
