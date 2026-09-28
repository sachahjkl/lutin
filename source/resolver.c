#include "resolver.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#ifdef ARM9
#include <nds.h>
enum { RESOLVER_STACK_BYTES = 16 * 1024 };
static cothread_t worker;
#else
#include <pthread.h>
#include <stdatomic.h>
static pthread_t worker;
static atomic_bool finished;
#endif

static bool active;
static char hostname[RESOLVER_HOST_BYTES];
static ResolverResult answer;

static void resolve(void) {
    struct addrinfo hints = {.ai_family = AF_UNSPEC,
                             .ai_socktype = SOCK_STREAM};
    struct addrinfo *addresses = NULL;
    answer.error = getaddrinfo(hostname, NULL, &hints, &addresses);
    if (!answer.error) {
        answer.error = EAI_NONAME;
        for (struct addrinfo *entry = addresses; entry;
             entry = entry->ai_next) {
            const void *address;
            if (entry->ai_family == AF_INET)
                address = &((struct sockaddr_in *)entry->ai_addr)->sin_addr;
            else if (entry->ai_family == AF_INET6)
                address = &((struct sockaddr_in6 *)entry->ai_addr)->sin6_addr;
            else
                continue;
            if (inet_ntop(entry->ai_family, address, answer.address,
                          sizeof(answer.address))) {
                answer.error = 0;
                break;
            }
        }
    }
    if (addresses)
        freeaddrinfo(addresses);
}

#ifdef ARM9
static int run(void *unused) {
    (void)unused;
    resolve();
    return 0;
}
#else
static void *run(void *unused) {
    (void)unused;
    resolve();
    atomic_store(&finished, true);
    return NULL;
}
#endif

bool resolver_poll(ResolverResult *result) {
    if (active) {
#ifdef ARM9
        if (!cothread_has_joined(worker))
            return false;
        cothread_delete(worker);
#else
        if (!atomic_load(&finished))
            return false;
        pthread_join(worker, NULL);
#endif
        active = false;
    }
    if (result)
        *result = answer;
    return true;
}

bool resolver_start(const char *host) {
    if (!host || !host[0] || strlen(host) >= sizeof(hostname) ||
        !resolver_poll(NULL))
        return false;
    snprintf(hostname, sizeof(hostname), "%s", host);
    size_t length = strlen(hostname);
    if (hostname[0] == '[' && hostname[length - 1] == ']') {
        memmove(hostname, hostname + 1, length - 2);
        hostname[length - 2] = 0;
    }
    memset(&answer, 0, sizeof(answer));
#ifdef ARM9
    worker = cothread_create(run, NULL, RESOLVER_STACK_BYTES, 0);
    active = worker >= 0;
#else
    atomic_store(&finished, false);
    active = pthread_create(&worker, NULL, run, NULL) == 0;
#endif
    return active;
}
