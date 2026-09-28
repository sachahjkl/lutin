#ifndef AI_RUNTIME_H
#define AI_RUNTIME_H

#include "input.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RUNTIME_SCREEN_WIDTH = 256,
    RUNTIME_SCREEN_HEIGHT = 192,
    RUNTIME_FRAMES_PER_SECOND = 60,
    RUNTIME_ASSET_FILE_BYTES = 32768,
    RUNTIME_ASSET_READ_BYTES = 65536,
    RUNTIME_SPRITE_MAX_SIZE = 64,
    RUNTIME_PALETTE_COLORS = 16,
    RUNTIME_SPRITE_MAX_SCALE = 8,
    RUNTIME_SOUND_MAX_NOTES = 128,
    RUNTIME_SOUND_MIN_FREQUENCY = 32,
    RUNTIME_SOUND_MAX_FREQUENCY = 16000,
    RUNTIME_SOUND_MAX_FRAMES = 60 * RUNTIME_FRAMES_PER_SECOND,
    RUNTIME_AUDIO_MAX_VOLUME = 127,
    RUNTIME_AUDIO_VOICES = 4,
    RUNTIME_NOISE_VOICE = RUNTIME_AUDIO_VOICES - 1
};

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
typedef char *(*RuntimeAssetReader)(const char *path, size_t limit);
typedef void (*RuntimeAudio)(unsigned voice, unsigned frequency,
                             unsigned volume);
void runtime_set_asset_reader(RuntimeAssetReader reader);
void runtime_set_audio(RuntimeAudio output);

#endif
