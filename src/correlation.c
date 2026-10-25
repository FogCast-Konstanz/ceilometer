#include "correlation.h"
#include <limits.h>
bool histogram_add(uint64_t h[PRN_LENGTH], const uint32_t *s, size_t n) {
    if (n < 2 || (n - 1) % PRN_LENGTH) return false;
    /* Validate first so failure is atomic. Two passes avoid an 8 KiB temporary
       on the Pico SDK's small default stack; this function is also reentrant. */
    const uint64_t limit = (uint64_t)INT64_MAX / (PRN_LENGTH * (PRN_LENGTH + 1u));
    for (unsigned bin = 0; bin < PRN_LENGTH; ++bin) {
        uint64_t addition = 0;
        for (size_t i = bin + 1u; i < n; i += PRN_LENGTH)
            addition += (uint16_t)(s[i] - s[i-1]);
        if (addition > limit || h[bin] > limit - addition) return false;
    }
    for (size_t i = 1; i < n; ++i)
        h[(i - 1u) % PRN_LENGTH] += (uint16_t)(s[i] - s[i-1]);
    return true;
}
void correlate(const uint64_t h[PRN_LENGTH], const uint8_t code[PRN_LENGTH], int64_t out[PRN_LENGTH]) {
    /* Integer, DC-rejected correlation: N*sum(h*bipolar shifted)-sum(h).
       Positive peak at d for h[k]=A*code[(k-d) mod N]+background.
       Divide by N on the host for count units. Bound enforced by histogram_add. */
    int64_t total = 0;
    for (unsigned k = 0; k < PRN_LENGTH; ++k) total += (int64_t)h[k];
    for (unsigned d = 0; d < PRN_LENGTH; ++d) {
        int64_t dot = 0;
        for (unsigned k = 0; k < PRN_LENGTH; ++k)
            dot += code[(k + PRN_LENGTH - d) % PRN_LENGTH] ? (int64_t)h[k] : -(int64_t)h[k];
        out[d] = dot * PRN_LENGTH - total;
    }
}
double range_m(unsigned lag, int offset_bins, uint32_t chip_hz) {
    return ((double)lag - offset_bins) * 299792458.0 / (2.0 * chip_hz);
}
