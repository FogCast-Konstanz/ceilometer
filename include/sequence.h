#pragma once
#include "config.h"
void prn_generate(uint8_t code[PRN_LENGTH]);
void prn_pack(const uint8_t code[PRN_LENGTH], uint32_t words[TX_WORDS]);
