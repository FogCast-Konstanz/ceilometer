#pragma once
#include <stdint.h>
#include <stdbool.h>
void adc_init(void);
void adc_gpio_init(unsigned pin);
void adc_set_temp_sensor_enabled(bool enable);
void adc_select_input(unsigned input);
uint16_t adc_read(void);
