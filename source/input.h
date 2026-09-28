#ifndef LUTIN_INPUT_H
#define LUTIN_INPUT_H
#include <stdbool.h>

enum {
    BUTTON_A = 1u << 0,
    BUTTON_B = 1u << 1,
    BUTTON_SELECT = 1u << 2,
    BUTTON_START = 1u << 3,
    BUTTON_RIGHT = 1u << 4,
    BUTTON_LEFT = 1u << 5,
    BUTTON_UP = 1u << 6,
    BUTTON_DOWN = 1u << 7,
    BUTTON_R = 1u << 8,
    BUTTON_L = 1u << 9,
    BUTTON_X = 1u << 10,
    BUTTON_Y = 1u << 11,
    BUTTON_TOUCH = 1u << 12
};

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
