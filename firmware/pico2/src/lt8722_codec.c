#include "lt8722_codec.h"
#include <string.h>
uint8_t lt8722_crc(const uint8_t *p, size_t n) {
    uint8_t crc = 0; /* LT8722 Rev B corrects the old datasheet seed to zero. */
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; ++b) crc = (uint8_t)((crc << 1) ^ ((crc & 128) ? 7 : 0));
    }
    return crc;
}
bool lt8722_encode(bool write, uint8_t reg, uint32_t value, uint8_t p[8]) {
    if (reg > 7) return false;
    memset(p, 0, 8); p[0] = write ? 0xf2 : 0xf4; p[1] = reg << 1;
    if (write) {
        for (unsigned i = 0; i < 4; ++i) p[2+i] = (uint8_t)(value >> (24-8*i));
        p[6] = lt8722_crc(p, 6);
    } else p[2] = lt8722_crc(p, 2);
    return true;
}
bool lt8722_decode(bool write, const uint8_t p[8], uint32_t *value, uint16_t *status) {
    if (p[7] != 0xa5 || lt8722_crc(p, write ? 2 : 6) != p[write ? 2 : 6]) return false;
    *status = ((uint16_t)p[0] << 8 | p[1]) & 0x7ff;
    if (!write) *value = (uint32_t)p[2] << 24 | (uint32_t)p[3] << 16 | (uint32_t)p[4] << 8 | p[5];
    return true;
}
