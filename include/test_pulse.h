#pragma once
#include <stdbool.h>
#include <stdint.h>
void test_pulse_init(void);
bool test_pulse_set(uint32_t hz);
uint32_t test_pulse_actual_hz(void);
