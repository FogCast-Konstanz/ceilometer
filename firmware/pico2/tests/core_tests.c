#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "sequence.h"
#include "correlation.h"
#include "protocol.h"
#include "lt8722_codec.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static uint8_t code[PRN_LENGTH];
static uint64_t histogram[PRN_LENGTH];
static int64_t profile[PRN_LENGTH];
static unsigned calls;
static char last_command[128], last_argument[128];
static void receive(char *cmd, char *arg, void *ctx) {
    (void)ctx; ++calls; strcpy(last_command, cmd); strcpy(last_argument, arg ? arg : "");
}
static void feed(protocol_parser *p, const char *s) { while (*s) protocol_feed(p, *s++, receive, NULL); }
int main(void) {
    prn_generate(code);
    unsigned ones = 0; bool seen[1024] = {0}; unsigned state = 1023;
    for (unsigned i = 0; i < PRN_LENGTH; ++i) {
        CHECK(state && !seen[state]); seen[state] = true;
        CHECK(code[i] == (state & 1)); ones += code[i];
        state = (state >> 1) | (((state ^ (state >> 3)) & 1) << 9);
    }
    CHECK(state == 1023 && ones == 512);
    uint32_t words[TX_WORDS]; prn_pack(code, words);
    for (unsigned b = 0; b < TX_WORDS * 32; ++b) CHECK(((words[b/32] >> (b%32)) & 1) == code[b%PRN_LENGTH]);
    for (unsigned d = 0; d < PRN_LENGTH; ++d) {
        int dot = 0;
        for (unsigned k = 0; k < PRN_LENGTH; ++k)
            dot += (code[k] ? 1 : -1) * (code[(k+d)%PRN_LENGTH] ? 1 : -1);
        CHECK(dot == (d ? -1 : 1023));
    }
    uint32_t snapshots[2 * PRN_LENGTH + 1]; snapshots[0] = 65530;
    for (unsigned i = 1; i <= 2 * PRN_LENGTH; ++i) {
        unsigned k = (i-1)%PRN_LENGTH;
        unsigned value = 7 + 3 * code[(k+PRN_LENGTH-37)%PRN_LENGTH];
        snapshots[i] = (snapshots[i-1] + value) & 65535;
    }
    CHECK(histogram_add(histogram, snapshots, 2 * PRN_LENGTH + 1));
    CHECK(!histogram_add(histogram, snapshots, 17));
    correlate(histogram, code, profile);
    for (unsigned i = 0; i < PRN_LENGTH; ++i) CHECK(profile[37] >= profile[i]);
    CHECK(profile[37] > 0 && profile[0] < 0);
    for (unsigned i = 0; i < PRN_LENGTH; ++i) histogram[i] = 123;
    correlate(histogram, code, profile);
    for (unsigned i = 0; i < PRN_LENGTH; ++i) CHECK(profile[i] == 0);
    histogram[3] = INT64_MAX;
    CHECK(!histogram_add(histogram, snapshots, PRN_LENGTH + 1));
    CHECK(histogram[0] == 123); /* overflow rejection is atomic */
    CHECK(range_m(1, 0, 10000000) > 14.98 && range_m(1, 0, 10000000) < 15.0);
    CHECK(range_m(37, 37, 10000000) == 0);
    protocol_parser p = {0}; feed(&p, "$set_threshold,123\r\n");
    CHECK(calls == 1 && !strcmp(last_command, "set_threshold") && !strcmp(last_argument, "123"));
    feed(&p, "garbage\n$stop\n"); CHECK(calls == 2);
    for (unsigned i = 0; i < 140; ++i) protocol_feed(&p, 'x', receive, NULL);
    feed(&p, "$stop\n"); CHECK(calls == 2); feed(&p, "$get_state\r"); CHECK(calls == 3);
    feed(&p, "$stop\x01\n"); CHECK(calls == 3);
    uint32_t n;
    CHECK(protocol_uint("4294967295", UINT32_MAX, &n) && n == UINT32_MAX);
    CHECK(!protocol_uint("4294967296", UINT32_MAX, &n));
    CHECK(!protocol_uint("-1", 65535, &n) && !protocol_uint("1,2", 65535, &n));
    CHECK(!protocol_uint("9", 1, &n) && !protocol_uint("", 65535, &n));
    CHECK(lt8722_crc((const uint8_t *)"123456789", 9) == 0xf4);
    uint8_t packet[8]; CHECK(lt8722_encode(true, 4, 0xff000000, packet));
    CHECK(packet[0] == 0xf2 && packet[1] == 8 && packet[2] == 0xff && packet[6] == lt8722_crc(packet,6));
    CHECK(!lt8722_encode(true, 8, 0, packet));
    CHECK(lt8722_encode(false, 1, 0, packet) && packet[2] == lt8722_crc(packet, 2));
    uint8_t rx[] = {0x01,0x23,0x12,0x34,0x56,0x78,0,0xa5};
    rx[6] = lt8722_crc(rx,6); uint32_t value = 0; uint16_t status = 0;
    CHECK(lt8722_decode(false, rx, &value, &status) && value == 0x12345678 && status == 0x123);
    rx[4] ^= 1; CHECK(!lt8722_decode(false,rx,&value,&status));
    rx[4] ^= 1; rx[7] = 0xc3; CHECK(!lt8722_decode(false,rx,&value,&status));
    uint8_t wr[] = {0x01,0x23,0,0,0,0,0,0xa5}; wr[2] = lt8722_crc(wr,2);
    CHECK(lt8722_decode(true, wr, &value, &status));
    puts("PASS: PRN period/packing/autocorrelation, rollover/histogram/overflow, delayed correlation/DC/range, protocol framing/bounds, LT8722 CRC/ACK/codec");
    return 0;
}
