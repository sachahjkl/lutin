#ifndef LUTIN_PLATFORM_AUDIO_H
#define LUTIN_PLATFORM_AUDIO_H
#include <nds.h>

static inline void platform_audio(unsigned voice, unsigned frequency,
                                  unsigned volume) {
    int channel = voice == 3 ? 14 : 8 + (int)voice;
    soundKill(channel);
    if (!frequency || !volume)
        return;
    if (voice == 3)
        soundPlayNoiseChannel(channel, frequency, volume, 64);
    else
        soundPlayPSGChannel(channel, DutyCycle_50, frequency, volume, 64);
}
#endif
