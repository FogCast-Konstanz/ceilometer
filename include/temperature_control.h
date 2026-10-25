#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pid_controller.h"
#define TEMPERATURE_MAX_AGE_US 2000000u
typedef struct {
    bool valid;
    int32_t mc;
    uint32_t sampled_us, sequence;
} temperature_sample;
typedef struct {
    PID_Controller pid;
    int32_t target_mc;
    int polarity, output_mv;
    bool enabled, fault;
    uint32_t last_sequence, last_sample_us;
} temperature_controller;
bool temperature_fresh(temperature_sample sample, uint32_t now);
bool temperature_laser_allowed(temperature_sample sample, uint32_t now);
bool temperature_tec_allowed(temperature_sample sample, uint32_t now);
void temperature_init(temperature_controller *control);
void temperature_stop(temperature_controller *control);
bool temperature_start(temperature_controller *control, temperature_sample sample, uint32_t now);
bool temperature_update(temperature_controller *control, temperature_sample sample, uint32_t now);
bool ds18b20_decode(const uint8_t scratchpad[9], int32_t *mc);
