#ifndef AI_RUNTIME_H
#define AI_RUNTIME_H

#include "input.h"
#include <cJSON.h>
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
enum {
    RUNTIME_MEMORY_BYTES = 2 * 1024 * 1024,
    RUNTIME_INSTRUCTIONS = 100000,
    RUNTIME_HOOK_INTERVAL = 1000,
    RUNTIME_PIXEL_BUDGET = 262144,
    RUNTIME_TEST_MAX_FRAMES = 120,
    RUNTIME_SAVE_BYTES = 8192,
    RUNTIME_STORAGE_OPERATIONS = 4,
    RUNTIME_STORAGE_BYTES = 2 * RUNTIME_SAVE_BYTES
};

typedef struct {
    uint16_t *pixels;
    unsigned held;
    unsigned pressed;
    int touch_x;
    int touch_y;
    bool touching;
} RuntimeInput;

typedef struct {
    unsigned loop_us, work_us, display_us, ui_us, agent_us;
    unsigned display_bytes, missed_vblanks;
} RuntimePlatformMetrics;
void runtime_platform_metrics(RuntimePlatformMetrics metrics);

bool runtime_start(const char *code);
void runtime_graphics_init(void);
bool runtime_start_test(const char *code, unsigned seed);
bool runtime_step(unsigned frames, unsigned buttons, int x, int y);
void runtime_finish_test(void);
cJSON *runtime_inspect(void);
void runtime_stop(void);
void runtime_frame(RuntimeInput input);
bool runtime_running(void);
bool runtime_testing(void);
const char *runtime_error(void);
size_t runtime_memory(void);
void runtime_set_font(const unsigned char *font);
bool runtime_capture(const char *path);
const uint16_t *runtime_pixels(void);
unsigned runtime_logs(unsigned cursor, char *output, size_t capacity);
typedef char *(*RuntimeAssetReader)(const char *path, size_t limit);
typedef bool (*RuntimeSaveWriter)(const char *path, const char *data);
void runtime_set_save_writer(RuntimeSaveWriter writer);
typedef void (*RuntimeAudio)(unsigned voice, unsigned frequency,
                             unsigned volume);
void runtime_set_asset_reader(RuntimeAssetReader reader);
void runtime_set_audio(RuntimeAudio output);

#endif
