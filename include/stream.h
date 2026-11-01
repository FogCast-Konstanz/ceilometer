#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "integration.h"
typedef struct {bool active,failed;uint32_t blocks,max_process_us;uint64_t started_us;uint32_t error_flags;} stream_status;
void stream_init(void);
bool stream_start(unsigned length,uint16_t seed,uint32_t hz,uint32_t period_us,unsigned discard,bool laser);
void stream_stop(void);
void stream_poll(void);
uint64_t stream_wall_ns(void);
stream_status stream_get_status(void);

void stream_debug(uint32_t values[8]);
