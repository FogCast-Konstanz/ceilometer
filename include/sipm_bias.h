#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "temperature_control.h"
typedef enum { BIAS_OK, BIAS_TEMPERATURE, BIAS_VOLTAGE_LIMIT, BIAS_BUILD_LIMIT, BIAS_DAC_FAILURE } bias_reason;
typedef struct {
    temperature_sample temperature;
    int64_t breakdown_uv,target_uv,estimated_uv;
    uint16_t code;
    bool enabled,fault,valid;
    bias_reason reason;
} bias_status;
/* Feedforward estimate, not a measured high voltage. All calculations use int64. */
bool sipm_bias_calculate(temperature_sample sample,uint32_t now,bias_status *out);
void sipm_bias_init(void);
void sipm_bias_reconfigure(void); /* invalidate feedback, retain any fault */
void sipm_bias_stop(void); /* SHDN first; preserves the fault latch. */
bool sipm_bias_enable(void);
bool sipm_bias_clear_fault(void);
bool sipm_bias_service(bool buses_available);
bias_status sipm_bias_status(void);
