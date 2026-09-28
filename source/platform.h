#ifndef AI_PLATFORM_H
#define AI_PLATFORM_H
#include <nds.h>

static inline void platform_wait_frame(void) {
    /* DSWiFi receives packets in a cooperative thread. */
    cothread_yield_irq(IRQ_VBLANK);
}
#endif
