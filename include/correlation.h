#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "config.h"
bool histogram_add(uint64_t histogram[PRN_LENGTH], const uint32_t *samples, size_t count);
void correlate(const uint64_t histogram[PRN_LENGTH], const uint8_t code[PRN_LENGTH], int64_t result[PRN_LENGTH]);
double range_m(unsigned lag, int offset_bins, uint32_t chip_hz);
