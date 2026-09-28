/* Exercise real DS sockets and libcurl with an injected connect failure. */
#include "network.h"
#include "platform.h"
#include <dswifi9.h>
#include <errno.h>
#include <nds.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

enum { CONNECTION_ATTEMPTS = 24, REQUEST_FRAME_LIMIT = 240 };
static unsigned connections;
static int connect_error;

int __wrap_Wifi_AssocStatus(void) { return ASSOCSTATUS_ASSOCIATED; }

int __real_getaddrinfo(const char *, const char *, const struct addrinfo *,
                       struct addrinfo **);
int __wrap_getaddrinfo(const char *host, const char *service,
                       const struct addrinfo *hints, struct addrinfo **result) {
    (void)host;
    return __real_getaddrinfo("192.0.2.1", service, hints, result);
}

int __wrap_connect(int socket, const struct sockaddr *address,
                   socklen_t length) {
    (void)socket;
    (void)address;
    (void)length;
    connections++;
    errno = connect_error;
    return -1;
}

int main(void) {
    defaultExceptionHandler();
    consoleDemoInit();
    consoleDebugInit(DebugDevice_NOCASH);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("NETWORK SOCKET CHECK\n");
    for (unsigned attempt = 0; attempt < CONNECTION_ATTEMPTS; attempt++) {
        unsigned previous_connections = connections;
        connect_error = attempt % 2 ? ENETUNREACH : ECONNREFUSED;
        if (!network_update_catalog())
            goto failure;
        unsigned frames = 0;
        while (network_busy() && frames++ < REQUEST_FRAME_LIMIT) {
            network_tick();
            platform_wait_frame();
        }
        char expected[32];
        snprintf(expected, sizeof(expected), "OS %d (", connect_error);
        if (network_busy() || !strstr(network_status(), expected) ||
            !strstr(network_status(), "IP: 192.0.2.1") ||
            connections <= previous_connections)
            goto failure;
        fprintf(stderr, "Socket attempt %u: %s\n", attempt + 1,
                network_status());
    }
    printf("SOCKET PASS\n%u attempts\n", CONNECTION_ATTEMPTS);
    fprintf(stderr, "SOCKET PASS\n");
    goto finished;
failure:
    printf("SOCKET FAIL\n%s\n%u connects\n", network_status(), connections);
finished:
    network_stop();
    while (1)
        platform_wait_frame();
}
