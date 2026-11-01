#pragma once
#include <stdbool.h>
#include "config.h"
void prn_tx_init(const uint8_t code[PRN_LENGTH]);
bool prn_tx_prepare(uint32_t chip_hz, bool laser);
void prn_tx_start(void);
void prn_tx_stop(void);
bool prn_tx_starved(void);

void prn_tx_set_code(const uint8_t code[PRN_LENGTH]);
void prn_tx_set_sequence(const uint8_t *code,unsigned length);
