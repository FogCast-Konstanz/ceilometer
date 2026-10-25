#pragma once
#include <stdbool.h>
#include <stdint.h>
#define clk_sys 0
bool set_sys_clock_khz(uint32_t khz, bool required);
uint32_t clock_get_hz(int clock);
