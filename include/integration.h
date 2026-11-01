#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PRN_MAX_LENGTH 4095u
/* One worker owns accumulation. Result publication is acquire/release atomic. */
typedef struct {
    uint32_t id, length, frames, chip_hz, period_us;
    uint64_t first_chip, end_chip, exposure_ns;
} integration_result;
bool integration_init(unsigned length,uint32_t hz,uint32_t period_us,unsigned discard_frames);
/* Restart only at a whole-frame boundary; never difference across a gap. */
bool integration_restart_burst(unsigned discard_frames);
unsigned integration_frames_remaining(void);
bool integration_push(uint32_t snapshot);
bool integration_block(const volatile uint32_t *samples,unsigned count);
void integration_commit(bool valid);
void integration_cancel(void);
const integration_result *integration_pending(void);
const uint64_t *integration_histogram(void);
bool integration_release(uint32_t id);
uint32_t integration_dropped(void);
bool prn_generate_length(uint8_t *code,unsigned length,uint16_t seed);
void prn_pack_length(const uint8_t *code,unsigned length,uint32_t *words);
