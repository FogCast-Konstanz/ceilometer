#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
uint8_t lt8722_crc(const uint8_t *p, size_t n);
bool lt8722_encode(bool write, uint8_t reg, uint32_t value, uint8_t packet[8]);
bool lt8722_decode(bool write, const uint8_t packet[8], uint32_t *value, uint16_t *status);
