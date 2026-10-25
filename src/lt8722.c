#include "lt8722.h"
#include "lt8722_codec.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
static bool enabled;
static bool transaction(bool write, uint8_t reg, uint32_t *value) {
    uint8_t tx[8], rx[8]; uint16_t status;
    if (!lt8722_encode(write, reg, *value, tx)) return false;
    gpio_put(PIN_SPI_CS, 0); busy_wait_us_32(1);
    int n = spi_write_read_blocking(spi1, tx, rx, 8);
    busy_wait_us_32(1); gpio_put(PIN_SPI_CS, 1);
    return n == 8 && lt8722_decode(write, rx, value, &status);
}
void lt8722_init(void) {
    gpio_init(PIN_TEC_SWEN); gpio_put(PIN_TEC_SWEN, 0); gpio_set_dir(PIN_TEC_SWEN, GPIO_OUT);
    gpio_init(PIN_SPI_CS); gpio_put(PIN_SPI_CS, 1); gpio_set_dir(PIN_SPI_CS, GPIO_OUT);
    spi_init(spi1, 1000000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI);
}
bool lt8722_read(uint8_t reg, uint32_t *v) { *v = 0; return transaction(false, reg, v); }
bool lt8722_write(uint8_t reg, uint32_t v) { return transaction(true, reg, &v); }
void lt8722_disable(void) {
    gpio_put(PIN_TEC_SWEN, 0); enabled = false;
    uint32_t v;
    if (lt8722_read(0, &v)) (void)lt8722_write(0, v & ~3u);
}
bool lt8722_healthy(void) {
    uint32_t status;
    return lt8722_read(1, &status) && !(status & 0x7e0u);
}
bool lt8722_enable(void) {
#if CEIL_ENABLE_TEC
    /* +/-~0.85 A nominal limits; raw register endpoints follow Rev B.
       Temperature feedback and PID are supervised by temperature_control. */
    uint32_t cmd;
    if (!lt8722_read(0, &cmd) || !lt8722_write(0, (cmd & ~3u) | 1u)) goto fail;
    sleep_ms(2);
    if (!lt8722_write(2, 64u) || !lt8722_write(3, 448u) ||
        !lt8722_write(4, 0xff000000u) || !lt8722_write(1, 0)) goto fail;
    sleep_ms(1);
    /* Signed DAC common-mode ramp from -2^24 to zero, >=5 ms. */
    for (int step = 15; step >= 0; --step) {
        if (!lt8722_write(4, (uint32_t)(-step * 1048576))) goto fail;
        sleep_us(400);
    }
    gpio_put(PIN_TEC_SWEN, 1);
    if (!lt8722_write(0, cmd | 3u)) goto fail;
    sleep_us(200);
    enabled = lt8722_healthy();
    if (enabled) return true;
fail:
    lt8722_disable();
#endif
    return false;
}
bool lt8722_set_mv(int mv) {
    if (!enabled || mv < -500 || mv > 500) return false;
    /* VTEC (LDR-SFB) = signed(code)*1.25*16*Ga/2^24; nominal Ga=0.969. */
    int32_t code = (int32_t)((int64_t)mv * 16777216 * 1000 / 19380000);
    if (!lt8722_write(4, (uint32_t)code)) { lt8722_disable(); return false; }
    return true;
}
