#ifndef LUTIN_PLATFORM_AUDIO_H
#define LUTIN_PLATFORM_AUDIO_H
#include "runtime.h"
#include <nds.h>

enum {
    AUDIO_FIRST_SQUARE_CHANNEL = 8,
    AUDIO_NOISE_CHANNEL = 14,
    AUDIO_CENTER_PAN = 64
};

static inline void platform_audio(unsigned voice, unsigned frequency,
                                  unsigned volume) {
    int channel = voice == RUNTIME_NOISE_VOICE
                      ? AUDIO_NOISE_CHANNEL
                      : AUDIO_FIRST_SQUARE_CHANNEL + (int)voice;
    soundKill(channel);
    if (!frequency || !volume)
        return;
    if (voice == RUNTIME_NOISE_VOICE)
        soundPlayNoiseChannel(channel, frequency, volume, AUDIO_CENTER_PAN);
    else
        soundPlayPSGChannel(channel, DutyCycle_50, frequency, volume,
                            AUDIO_CENTER_PAN);
}
#endif
