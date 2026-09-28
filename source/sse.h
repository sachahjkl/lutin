#ifndef AI_SSE_H
#define AI_SSE_H
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    char line[65536];
    char event[65536];
    size_t line_size;
    size_t event_size;
    void (*callback)(const char *);
} SseParser;
void sse_init(SseParser *parser, void (*callback)(const char *));
bool sse_feed(SseParser *parser, const char *data, size_t length);
#endif
