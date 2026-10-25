#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "temperature_control.h"
void sensors_init(void);
void sensors_poll(void); /* call while acquisition is idle */
bool sensors_receiver_mc(int32_t *temperature);
bool sensors_laser_mc(int32_t *temperature);
temperature_sample sensors_laser_sample(void);
