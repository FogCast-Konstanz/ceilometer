#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "temperature_control.h"
void sensors_init(void);
void sensors_poll(void); /* call while acquisition is idle */
bool sensors_receiver_mc(int32_t *temperature);
bool sensors_laser_mc(int32_t *temperature);
temperature_sample sensors_laser_sample(void);

temperature_sample sensors_laser_uncalibrated(void);
const uint8_t *sensors_scratchpad(void);
void sensors_errors(uint32_t *presence,uint32_t *crc,uint32_t *conversion);
bool sensors_receiver_register(unsigned reg,uint16_t *value);
bool sensors_receiver_configure(void);
