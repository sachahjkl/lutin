#include "runtime.h"
#include "workspace.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned frequency[4];
static void audio(unsigned voice, unsigned value, unsigned volume) {
    assert(voice < 4 && volume <= 127);
    frequency[voice] = value;
}

int main(void) {
    workspace_init(true, ".");
    runtime_set_asset_reader(workspace_read);
    runtime_set_audio(audio);
    char *code = workspace_read("main.lua", 32768);
    assert(code && runtime_start(code));
    free(code);
    uint16_t pixels[256 * 192] = {0};
    RuntimeInput input = {.pixels = pixels};
    runtime_frame(input);
    assert(runtime_running() && frequency[0] == 262);
    assert(pixels[80 * 256 + 120] != pixels[0]);
    input.pressed = 3;
    runtime_frame(input);
    assert(frequency[1] == 440 && frequency[3] == 2000);
    input.pressed = 0;
    for (unsigned frame = 0; frame < 200; frame++)
        runtime_frame(input);
    assert(runtime_running() && !runtime_error()[0]);
    assert(frequency[1] == 0 && frequency[3] == 0);
    runtime_stop();
    assert(frequency[0] == 0);
    puts("Media: bundled sprite animation, music, effects and cleanup passed");
    return 0;
}
