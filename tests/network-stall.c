/* Fault injection for the production ROM's resolver and network state machine.
 */
#include <dswifi9.h>
#include <nds.h>
#include <netdb.h>
#include <stdio.h>

enum { STALLED_DNS_FRAMES = 30 * 60 };

bool __wrap_Wifi_InitDefault(unsigned flags) {
    (void)flags;
    consoleDebugInit(DebugDevice_NOCASH);
    return true;
}

void __wrap_Wifi_AutoConnect(void) {}
int __wrap_Wifi_AssocStatus(void) { return ASSOCSTATUS_ASSOCIATED; }

int __wrap_getaddrinfo(const char *host, const char *service,
                       const struct addrinfo *hints, struct addrinfo **result) {
    (void)service;
    (void)hints;
    static unsigned lookups;
    lookups++;
    fprintf(stderr, "DNS fixture enter %u: %s\n", lookups, host);
    if (lookups == 1)
        for (unsigned frame = 0; frame < STALLED_DNS_FRAMES; frame++)
            cothread_yield_irq(IRQ_VBLANK);
    *result = NULL;
    fprintf(stderr, "DNS fixture exit %u\n", lookups);
    return EAI_FAIL;
}
