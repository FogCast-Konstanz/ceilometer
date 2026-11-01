#pragma once
#include <stdint.h>
typedef struct { uint32_t cs; } fake_adc_hw;
extern fake_adc_hw *adc_hw;
#define ADC_CS_ERR_BITS 1u
