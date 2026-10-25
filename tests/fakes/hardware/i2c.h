#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct { int unused; } i2c_inst_t;
extern i2c_inst_t *i2c1;
int i2c_write_timeout_us(i2c_inst_t *, uint8_t, const uint8_t *, size_t, bool, uint32_t);
int i2c_read_timeout_us(i2c_inst_t *, uint8_t, uint8_t *, size_t, bool, uint32_t);

extern i2c_inst_t *i2c0;
void i2c_init(i2c_inst_t *bus,unsigned baud);
