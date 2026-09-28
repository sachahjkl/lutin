#ifndef LUTIN_RESOLVER_H
#define LUTIN_RESOLVER_H
#include <stdbool.h>

enum { RESOLVER_HOST_BYTES = 256, RESOLVER_ADDRESS_BYTES = 46 };
typedef struct {
    char address[RESOLVER_ADDRESS_BYTES];
    int error;
} ResolverResult;

/* One worker owns its input and result until the lookup returns. */
bool resolver_start(const char *host);
bool resolver_poll(ResolverResult *result);
#endif
