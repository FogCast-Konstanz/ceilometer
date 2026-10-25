#pragma once
#include <stdint.h>
#include <stdbool.h>
void watchdog_enable(uint32_t ms,bool pause);
void watchdog_update(void);
