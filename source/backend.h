#ifndef AI_BACKEND_H
#define AI_BACKEND_H
#include "protocol.h"

typedef struct {
    const char *id, *base_url, *auth_header, *session_header;
} Provider;

typedef struct {
    const char *id, *label, *model;
    const Provider *provider;
    ApiProtocol protocol;
} Backend;

static const Provider providers[] = {
    {"opencode-go", "https://opencode.ai/zen/go/v1", "Authorization: Bearer",
     "x-opencode-session"}};

static const Backend backends[] = {
    {"opencode-go/gpt-6-luna", "Go / GPT 6 Luna", "gpt-6-luna", &providers[0],
     API_RESPONSES},
    {"opencode-go/gpt-5.6-luna", "Go / GPT 5.6 Luna", "gpt-5.6-luna",
     &providers[0], API_RESPONSES},
    {"opencode-go/grok-4.6", "Go / Grok 4.6", "grok-4.6", &providers[0],
     API_RESPONSES},
    {"opencode-go/deepseek-v4-flash", "Go / DeepSeek V4 Flash",
     "deepseek-v4-flash", &providers[0], API_CHAT_COMPLETIONS}};
#define BACKEND_COUNT (sizeof(backends) / sizeof(*backends))
static inline ApiProtocol backend_protocol(unsigned index) {
    return backends[index].protocol;
}
#endif
