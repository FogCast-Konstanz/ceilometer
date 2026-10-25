#include "sensors.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
static int32_t laser_mc;
static bool pending, valid;
static uint32_t converted_at, last_request;
static void ow_low(void) { gpio_put(PIN_ONEWIRE, 0); gpio_set_dir(PIN_ONEWIRE, GPIO_OUT); }
static void ow_release(void) { gpio_set_dir(PIN_ONEWIRE, GPIO_IN); }
static bool ow_reset(void) {
    ow_low(); busy_wait_us_32(480); ow_release();
    uint32_t irq = save_and_disable_interrupts();
    busy_wait_us_32(70); bool presence = !gpio_get(PIN_ONEWIRE);
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
static uint8_t crc8(const uint8_t *p, unsigned n) {
    uint8_t crc = 0;
    while (n--) {
        uint8_t v = *p++;
        for (unsigned b = 0; b < 8; ++b, v >>= 1) {
            unsigned mix = (crc ^ v) & 1; crc >>= 1; if (mix) crc ^= 0x8c;
        }
    }
    return crc;
}
void sensors_init(void) { gpio_init(PIN_ONEWIRE); ow_release(); }
void sensors_poll(void) {
    uint32_t now = time_us_32();
    if (pending && now - converted_at >= 750000) {
        pending = false; valid = false;
        if (ow_reset()) {
            uint8_t p[9]; ow_write(0xcc); ow_write(0xbe);
            for (unsigned i = 0; i < 9; ++i) p[i] = ow_read();
            int16_t raw = (int16_t)((uint16_t)p[1] << 8 | p[0]);
            if (!crc8(p, 9) && p[5] == 0xff && p[7] == 0x10 && raw >= -880 && raw <= 2000) {
                laser_mc = (int32_t)raw * 1000 / 16; valid = true;
            }
        }
    }
    if (!pending && now - last_request >= 1000000) {
        last_request = now;
        if (ow_reset()) {
            ow_write(0xcc); ow_write(0x44); converted_at = time_us_32(); pending = true;
        } else valid = false;
    }
}
bool sensors_laser_mc(int32_t *t) {
    if (!valid || time_us_32() - converted_at > 3000000) return false;
    *t = laser_mc; return true;
}
bool sensors_receiver_mc(int32_t *t) {
    uint8_t reg = 0, p[2];
    if (i2c_write_timeout_us(i2c1, I2C_TEMPERATURE_ADDRESS, &reg, 1, true, 2000) != 1 ||
        i2c_read_timeout_us(i2c1, I2C_TEMPERATURE_ADDRESS, p, 2, false, 2000) != 2) return false;
    int16_t raw = (int16_t)((uint16_t)p[0] << 8 | p[1]);
    *t = (int32_t)raw * 1000 / 256; return true;
}
