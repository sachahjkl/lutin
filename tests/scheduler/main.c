#include "platform.h"
#include "platform_input.h"
#include <stdio.h>

static unsigned background_frames;

static int background(void *argument) {
    (void)argument;
    while (1) {
        background_frames++;
        cothread_yield_irq(IRQ_VBLANK);
    }
    return 0;
}

int main(void) {
    consoleDemoInit();
    platform_input_init();
    cothread_t worker =
        cothread_create(background, NULL, 4096, COTHREAD_DETACHED);
    for (unsigned frame = 0; frame < 90; frame++)
        swiWaitForVBlank();
    unsigned blocked_frames = background_frames;
    background_frames = 0;
    for (unsigned frame = 0; frame < 90; frame++)
        platform_wait_frame();
    bool passed =
        worker != -1 && blocked_frames < 10 && background_frames >= 80;
    printf("SCHEDULER\n%s\nBIOS wait: %u\nScheduler wait: %u\n",
           passed ? "PASS" : "FAIL", blocked_frames, background_frames);
    printf("START: input stall test\n");
    while (!(platform_input_take().pressed & KEY_START))
        platform_wait_frame();
    printf("INPUT STALL: tap A now\n");
    for (unsigned frame = 0; frame < 120; frame++)
        swiWaitForVBlank();
    InputSample captured = platform_input_take();
    bool retained = (captured.pressed & KEY_A) && !(captured.held & KEY_A);
    bool consumed = !(platform_input_take().pressed & KEY_A);
    printf("INPUT %s\n", retained && consumed ? "PASS" : "FAIL");
    while (1)
        platform_wait_frame();
}
