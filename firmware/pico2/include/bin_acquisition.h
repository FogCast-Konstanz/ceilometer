#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { bool busy, ready, failed, tx_starved; uint32_t elapsed_us, samples; uint64_t completed_us; } acquisition_status;
void bin_acquisition_init(void);
bool bin_acquisition_start(unsigned frames, uint32_t hz, bool laser);
bool bin_acquisition_start_sequence(unsigned frames,unsigned length,uint32_t hz,bool laser);
void bin_acquisition_stop(void);
void bin_acquisition_poll(void);
acquisition_status bin_acquisition_status(void);
const uint32_t *bin_acquisition_data(void);
