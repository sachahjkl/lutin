#include "network.h"
#include <assert.h>
#include <dswifi9.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static time_t now = 100;
static unsigned attempts;
time_t __wrap_time(time_t *output) {
    if (output)
        *output = now;
    return now;
}
bool Wifi_InitDefault(unsigned flags) {
    assert(flags == (INIT_ONLY | WIFI_ATTEMPT_DSI_MODE));
    return true;
}
void Wifi_AutoConnect(void) { attempts++; }
int Wifi_AssocStatus(void) { return ASSOCSTATUS_CANNOTCONNECT; }

int main(void) {
    assert(mkdir("keys", 0700) == 0);
    network_set_directory("keys");
    network_set_backend(0, "test-session");
    assert(!network_start("{}", NULL));
    assert(strstr(network_status(), "opencode-key"));
    FILE *file = fopen("keys/opencode-key", "wb");
    assert(file && fputs("test-key\n", file) >= 0 && fclose(file) == 0);
    assert(network_start("{}", NULL));
    assert(!strcmp(network_wifi(), "JOIN"));
    assert(attempts == 1);
    network_tick();
    network_tick();
    assert(attempts == 1 && strstr(network_status(), "retry"));
    now += 5;
    network_tick();
    assert(attempts == 2 && network_busy());
    now += 5;
    network_tick();
    assert(attempts == 3 && !network_busy());
    assert(strstr(network_status(), "3 attempts"));
    network_tick();
    assert(attempts == 4 && !network_busy());
    network_tick();
    assert(attempts == 4);
    now += 30;
    network_tick();
    assert(attempts == 5);
    assert(network_start("{}", NULL));
    network_stop();
    assert(!network_busy());
    puts("Network: separate Go key, DSi mode, bounded retries, idle reconnect "
         "and cancellation passed");
    return 0;
}
