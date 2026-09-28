#include "runtime.h"
#include "workspace.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    MUSIC_VOICE = 0,
    JUMP_VOICE = 1,
    MUSIC_FIRST_FREQUENCY = 262,
    JUMP_FIRST_FREQUENCY = 440,
    HIT_FIRST_FREQUENCY = 2000,
    SPRITE_SAMPLE_X = 120,
    SPRITE_SAMPLE_Y = 80,
    PLAYBACK_TEST_FRAMES = 200
};

static unsigned frequency[RUNTIME_AUDIO_VOICES];
static void audio(unsigned voice, unsigned value, unsigned volume) {
    assert(voice < RUNTIME_AUDIO_VOICES && volume <= RUNTIME_AUDIO_MAX_VOLUME);
    frequency[voice] = value;
}

int main(void) {
    workspace_init(true, ".");
    runtime_set_asset_reader(workspace_read);
    runtime_set_audio(audio);
    char *code = workspace_read("main.lua", RUNTIME_ASSET_FILE_BYTES);
    assert(code && runtime_start(code));
    free(code);
    uint16_t pixels[RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT] = {0};
    RuntimeInput input = {.pixels = pixels};
    runtime_frame(input);
    assert(runtime_running() &&
           frequency[MUSIC_VOICE] == MUSIC_FIRST_FREQUENCY);
    assert(pixels[SPRITE_SAMPLE_Y * RUNTIME_SCREEN_WIDTH + SPRITE_SAMPLE_X] !=
           pixels[0]);
    input.pressed = BUTTON_A | BUTTON_B;
    runtime_frame(input);
    assert(frequency[JUMP_VOICE] == JUMP_FIRST_FREQUENCY &&
           frequency[RUNTIME_NOISE_VOICE] == HIT_FIRST_FREQUENCY);
    input.pressed = 0;
    for (unsigned frame = 0; frame < PLAYBACK_TEST_FRAMES; frame++)
        runtime_frame(input);
    assert(runtime_running() && !runtime_error()[0]);
    assert(frequency[JUMP_VOICE] == 0 && frequency[RUNTIME_NOISE_VOICE] == 0);
    runtime_stop();
    assert(frequency[MUSIC_VOICE] == 0);
    puts("Media: bundled sprite animation, music, effects and cleanup passed");
    return 0;
}
