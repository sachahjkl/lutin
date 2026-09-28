#ifndef AI_RUNTIME_H
#define AI_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t *pixels;
    unsigned held;
    unsigned pressed;
    int touch_x;
    int touch_y;
    bool touching;
} RuntimeInput;

bool runtime_start(const char *code);
void runtime_stop(void);
void runtime_frame(RuntimeInput input);
bool runtime_running(void);
const char *runtime_error(void);
size_t runtime_memory(void);
void runtime_set_font(const unsigned char *font);
bool runtime_capture(const char *path);
unsigned runtime_logs(unsigned cursor, char *output, size_t capacity);

#endif
