#pragma once
#include <stdbool.h>
#include <stdint.h>
void board_init(void);
void board_outputs_off(void);
bool board_dac(unsigned which, uint16_t code);
void board_bias_enable(bool enable);
bool board_laser_fault(void);
uint16_t board_adc(unsigned channel);

bool board_dac_read(unsigned which,uint16_t *code);
uint16_t board_dac_requested(unsigned which);
bool board_adc_sample(unsigned channel,unsigned average,uint16_t *raw);
bool board_pin(unsigned pin);
