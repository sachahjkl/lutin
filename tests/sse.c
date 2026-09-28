#include "sse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned count;
static char result[128];
static void event(const char *value) {
    count++;
    snprintf(result, sizeof(result), "%s", value);
}

int main(void) {
    SseParser parser;
    sse_init(&parser, event);
    const char *stream = ": keepalive\r\nevent: message\r\ndata: {\r\ndata: "
                         "\"ok\":true}\r\n\r\n";
    for (size_t i = 0; i < strlen(stream); i++)
        assert(sse_feed(&parser, stream + i, 1));
    assert(count == 1);
    assert(strcmp(result, "{\n\"ok\":true}") == 0);
    const char *two = "data:1\n\ndata: 2\n\n";
    assert(sse_feed(&parser, two, strlen(two)));
    assert(count == 3 && strcmp(result, "2") == 0);
    assert(!sse_feed(&parser, "\0", 1));
    sse_init(&parser, event);
    char large[65536];
    memset(large, 'x', sizeof(large));
    assert(!sse_feed(&parser, large, sizeof(large)));
    puts("SSE: fragments, CRLF, multiline, multiple events and limits passed");
    return 0;
}
