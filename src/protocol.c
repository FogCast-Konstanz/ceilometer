#include "protocol.h"
#include <string.h>
void protocol_feed(protocol_parser *p, char c, protocol_handler handler, void *ctx) {
    if (c == '\r' || c == '\n') {
        if (!p->overflow && p->used && p->line[0] == '$') {
            p->line[p->used] = 0;
            char *arg = strchr(p->line, ',');
            if (arg) *arg++ = 0;
            handler(p->line + 1, arg, ctx);
        }
        p->used = 0; p->overflow = false;
    } else if ((unsigned char)c < 32 || (unsigned char)c > 126) {
        p->overflow = true;
    } else if (!p->overflow) {
        if (p->used == PROTOCOL_LINE_MAX) p->overflow = true;
        else p->line[p->used++] = c;
    }
}
bool protocol_uint(const char *s, uint32_t max, uint32_t *v) {
    if (!s || !*s) return false;
    uint32_t n = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        unsigned digit = (unsigned)(*s - '0');
        if (digit > max || n > (max - digit) / 10u) return false;
        n = n * 10u + digit;
    }
    *v = n; return true;
}
