#pragma once
#include <stdbool.h>
#include <stdint.h>
void lt8722_init(void);
bool lt8722_read(uint8_t reg, uint32_t *value);
bool lt8722_write(uint8_t reg, uint32_t value);
bool lt8722_enable(void);
void lt8722_disable(void);
bool lt8722_set_mv(int mv);
bool lt8722_healthy(void);
