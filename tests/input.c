#include "input.h"
#include <assert.h>
#include <stdio.h>

enum { A = 1, RIGHT = 16, START = 8, TOUCH = 4096 };
int main(void) {
    InputBuffer buffer = {0};
    input_sample(&buffer, A, 0, 0, TOUCH);
    input_sample(&buffer, 0, 0, 0, TOUCH);
    InputSample sample = input_take(&buffer);
    assert(sample.pressed == A && sample.held == 0);
    sample = input_game(&buffer, sample, true, START);
    assert(sample.pressed == A && sample.held == A);
    assert(input_take(&buffer).pressed == 0);

    input_sample(&buffer, RIGHT, 0, 0, TOUCH);
    sample = input_game(&buffer, input_take(&buffer), false, START);
    assert(!sample.held && !sample.pressed);
    input_sample(&buffer, RIGHT | A, 0, 0, TOUCH);
    sample = input_game(&buffer, input_take(&buffer), true, START);
    assert(sample.held == A && sample.pressed == A);
    input_sample(&buffer, 0, 0, 0, TOUCH);
    input_sample(&buffer, RIGHT | START, 0, 0, TOUCH);
    sample = input_game(&buffer, input_take(&buffer), true, START);
    assert(sample.held == RIGHT && sample.pressed == RIGHT);

    input_sample(&buffer, TOUCH, 80, 120, TOUCH);
    input_sample(&buffer, 0, 0, 0, TOUCH);
    sample = input_take(&buffer);
    assert((sample.pressed & TOUCH) && sample.touch_x == 80 &&
           sample.touch_y == 120);
    puts("Input: short taps, consumption, menu isolation and touch coordinates "
         "passed");
    return 0;
}
