#include "sse.h"
#include <string.h>

void sse_init(SseParser *parser, void (*callback)(const char *)) {
    parser->line_size = 0;
    parser->event_size = 0;
    parser->callback = callback;
}

bool sse_feed(SseParser *parser, const char *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (!data[i])
            return false;
        if (data[i] != '\n') {
            if (parser->line_size + 1 >= sizeof(parser->line))
                return false;
            parser->line[parser->line_size++] = data[i];
            continue;
        }
        if (parser->line_size && parser->line[parser->line_size - 1] == '\r')
            parser->line_size--;
        parser->line[parser->line_size] = 0;
        if (!parser->line_size) {
            if (parser->event_size && parser->callback) {
                parser->event[parser->event_size - 1] = 0;
                parser->callback(parser->event);
            }
            parser->event_size = 0;
        } else if (strncmp(parser->line, "data:", 5) == 0) {
            const char *value = parser->line + 5;
            if (*value == ' ')
                value++;
            size_t size = strlen(value);
            if (parser->event_size + size + 1 >= sizeof(parser->event))
                return false;
            memcpy(parser->event + parser->event_size, value, size);
            parser->event_size += size;
            parser->event[parser->event_size++] = '\n';
        }
        parser->line_size = 0;
    }
    return true;
}
