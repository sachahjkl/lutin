#ifndef AI_BACKEND_H
#define AI_BACKEND_H
#include "protocol.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *id, *base_url, *auth_header, *session_header;
} Provider;

typedef struct {
    const char *id, *label, *model;
    const Provider *provider;
    ApiProtocol protocol;
    const char *reasoning_effort;
} Backend;

enum {
    CATALOG_MAX_BYTES = 65536,
    CATALOG_MAX_PROVIDERS = 16,
    CATALOG_MAX_MODELS = 128
};
extern const Backend *backends;
extern unsigned backend_count;
#define BACKEND_COUNT backend_count
bool catalog_load(const char *directory);
bool catalog_install(const char *directory, const char *data, size_t length);
const char *catalog_error(void);
unsigned catalog_default(void);
static inline ApiProtocol backend_protocol(unsigned index) {
    return backends[index].protocol;
}
#endif
