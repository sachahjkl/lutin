#include "network.h"
#include "backend.h"
#include "resolver.h"
#include <assert.h>
#include <dswifi9.h>
#include <netdb.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static time_t now = 100;
static unsigned attempts;
static int association = ASSOCSTATUS_CANNOTCONNECT;
static atomic_bool lookup_started, release_lookup;
static bool lookup_success;

int __real_getaddrinfo(const char *, const char *, const struct addrinfo *,
                       struct addrinfo **);
int __wrap_getaddrinfo(const char *host, const char *service,
                       const struct addrinfo *hints, struct addrinfo **result) {
    assert(host && host[0]);
    atomic_store(&lookup_started, true);
    while (!atomic_load(&release_lookup))
        nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
    if (lookup_success)
        return __real_getaddrinfo("127.0.0.1", service, hints, result);
    *result = NULL;
    return EAI_FAIL;
}

static void wait_for_lookup(void) {
    while (!atomic_load(&lookup_started))
        nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
}

static void finish_lookup(void) {
    atomic_store(&release_lookup, true);
    while (!resolver_poll(NULL))
        nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
    atomic_store(&lookup_started, false);
}
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
int Wifi_AssocStatus(void) { return association; }

int main(void) {
    assert(catalog_load("."));
    assert(mkdir("credentials", 0700) == 0);
    assert(mkdir("credentials/keys", 0700) == 0);
    network_set_directory("credentials");
    network_set_backend(0, "test-session");
    assert(!network_start("{}", NULL));
    assert(strstr(network_status(), "keys/opencode-go"));
    FILE *file = fopen("credentials/keys/opencode-go", "wb");
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
    association = ASSOCSTATUS_ASSOCIATED;
    lookup_success = true;
    assert(network_start("{}", NULL));
    network_tick();
    wait_for_lookup();
    network_tick();
    assert(network_busy() && strstr(network_status(), "DNS"));
    network_stop();
    assert(unlink("credentials/keys/opencode-go") == 0);
    assert(network_update_catalog() && network_busy());
    assert(!network_start("{}", NULL) && network_busy());
    network_tick();
    assert(!network_busy() &&
           strstr(network_status(), "DNS worker unavailable"));
    finish_lookup();
    assert(!network_busy() && !network_catalog_updated());
    assert(strstr(network_status(), "DNS worker unavailable"));
    lookup_success = false;
    atomic_store(&release_lookup, false);

    assert(network_update_catalog());
    assert(!strcmp(network_wifi(), "ON"));
    assert(strstr(network_status(), "Wi-Fi connected"));
    network_tick();
    wait_for_lookup();
    for (unsigned frame = 0; frame < 120; frame++) {
        network_tick();
        assert(network_busy() && strstr(network_status(), "DNS"));
    }
    network_stop();
    assert(!network_busy());
    /* A cancelled worker keeps its storage until the system resolver returns.
     */
    assert(network_update_catalog());
    network_tick();
    assert(!network_busy() &&
           strstr(network_status(), "DNS worker unavailable"));
    finish_lookup();
    assert(!network_busy() && !network_catalog_updated());
    assert(strstr(network_status(), "DNS worker unavailable"));

    atomic_store(&release_lookup, false);
    assert(network_update_catalog());
    network_tick();
    wait_for_lookup();
    now += 60;
    network_tick();
    assert(!network_busy() && strstr(network_status(), "DNS timeout"));
    finish_lookup();
    assert(strstr(network_status(), "DNS timeout"));

    assert(network_update_catalog());
    network_tick();
    wait_for_lookup();
    finish_lookup();
    network_tick();
    assert(!network_busy() && strstr(network_status(), "DNS lookup failed"));

    lookup_success = true;
    assert(network_update_catalog());
    network_tick();
    wait_for_lookup();
    finish_lookup();
    network_tick();
    assert(network_busy() && strstr(network_status(), "Connecting to server"));
    network_stop();
    ResolverResult result;
    assert(resolver_poll(&result) && !result.error);
    assert(!strcmp(result.address, "127.0.0.1"));
    puts("Network: separate Go key, DSi mode, bounded retries, idle reconnect "
         "and DNS worker cancellation, timeout, failure and success passed");
    return 0;
}
