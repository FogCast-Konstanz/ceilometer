#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PROTOCOL_LINE_MAX 127u
typedef void (*protocol_handler)(char *command, char *argument, void *context);
typedef struct { char line[PROTOCOL_LINE_MAX + 1]; unsigned used; bool overflow; } protocol_parser;
void protocol_feed(protocol_parser *p, char c, protocol_handler handler, void *context);
bool protocol_uint(const char *s, uint32_t max, uint32_t *value);
