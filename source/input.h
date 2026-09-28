#ifndef LUTIN_INPUT_H
#define LUTIN_INPUT_H
#include <stdbool.h>

typedef struct {
    unsigned held, pressed;
    int touch_x, touch_y;
} InputSample;

typedef struct {
    InputSample pending;
    unsigned blocked;
} InputBuffer;

/* Callers serialize the interrupt producer and the main-loop consumer. */
static inline void input_sample(InputBuffer *buffer, unsigned held, int x,
                                int y, unsigned touch_mask) {
    buffer->pending.pressed |= held & ~buffer->pending.held;
    if ((held & touch_mask) && !(buffer->pending.held & touch_mask)) {
        buffer->pending.touch_x = x;
        buffer->pending.touch_y = y;
    }
    buffer->pending.held = held;
    buffer->blocked &= held;
}

static inline InputSample input_take(InputBuffer *buffer) {
    InputSample sample = buffer->pending;
    buffer->pending.pressed = 0;
    return sample;
}

static inline InputSample input_game(InputBuffer *buffer, InputSample sample,
                                     bool enabled, unsigned reserved) {
    if (!enabled) {
        buffer->blocked |= sample.held;
        sample.held = sample.pressed = 0;
    } else {
        sample.pressed &= ~(buffer->blocked | reserved);
        sample.held =
            (sample.held | sample.pressed) & ~(buffer->blocked | reserved);
    }
    return sample;
}
#endif
