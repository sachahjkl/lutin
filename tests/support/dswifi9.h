#ifndef TEST_WIFI_H
#define TEST_WIFI_H
#include <stdbool.h>
#define INIT_ONLY 0
#define ASSOCSTATUS_DISCONNECTED 0
#define WIFI_ATTEMPT_DSI_MODE (1 << 3)
#define ASSOCSTATUS_ASSOCIATED 1
#define ASSOCSTATUS_CANNOTCONNECT 2
#ifdef TEST_WIFI_CONTROL
bool Wifi_InitDefault(unsigned flags);
void Wifi_AutoConnect(void);
int Wifi_AssocStatus(void);
#else
static inline bool Wifi_InitDefault(unsigned flags) {
    return flags == (INIT_ONLY | WIFI_ATTEMPT_DSI_MODE);
}
static inline void Wifi_AutoConnect(void) {}
static inline int Wifi_AssocStatus(void) { return ASSOCSTATUS_ASSOCIATED; }
#endif
#endif
