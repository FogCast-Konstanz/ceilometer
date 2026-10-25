#include "sequence.h"
void prn_generate(uint8_t code[PRN_LENGTH]) {
    /* Right-shift Fibonacci, seed 0x3ff, output bit 0, feedback bit0 XOR bit3.
       Recurrence b[n+10] = b[n+3] XOR b[n], polynomial x^10+x^3+1. */
    uint16_t state = 0x3ff;
    for (unsigned i = 0; i < PRN_LENGTH; ++i) {
        code[i] = state & 1u;
        state = (state >> 1) | (((state ^ (state >> 3)) & 1u) << 9);
    }
}
void prn_pack(const uint8_t code[PRN_LENGTH], uint32_t words[TX_WORDS]) {
    for (unsigned w = 0; w < TX_WORDS; ++w) {
        words[w] = 0;
        for (unsigned b = 0; b < 32; ++b)
            words[w] |= (uint32_t)code[(w * 32 + b) % PRN_LENGTH] << b;
    }
}
