#ifndef LUTIN_PLATFORM_INPUT_H
#define LUTIN_PLATFORM_INPUT_H
#include "input.h"
#include <nds.h>

static InputBuffer input_buffer;
static volatile unsigned platform_vblanks;

static void sample_input(void) {
    platform_vblanks++;
    scanKeys();
    touchPosition touch = {0};
    touchRead(&touch);
    input_sample(&input_buffer, keysHeld(), touch.px, touch.py, KEY_TOUCH);
}

static inline void platform_input_init(void) {
    irqSet(IRQ_VBLANK, sample_input);
    irqEnable(IRQ_VBLANK);
}

static inline InputSample platform_input_take(void) {
    int critical = enterCriticalSection();
    InputSample sample = input_take(&input_buffer);
    leaveCriticalSection(critical);
    return sample;
}

static inline InputSample platform_input_game(InputSample sample,
                                              bool enabled) {
    int critical = enterCriticalSection();
    InputSample game =
        input_game(&input_buffer, sample, enabled, KEY_START | KEY_LID);
    leaveCriticalSection(critical);
    return game;
}
#endif
