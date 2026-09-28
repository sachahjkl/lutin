#ifndef AI_BACKEND_H
#define AI_BACKEND_H
#include "protocol.h"
typedef struct {
    const char *label, *model, *url, *key_file, *auth_header;
    ApiProtocol protocol;
} Backend;
static const Backend backends[] = {
    {"Go / GPT 6 Luna", "gpt-6-luna", "https://opencode.ai/zen/go/v1/responses",
     "opencode-key", "Authorization", API_RESPONSES},
    {"Go / GPT 5.6 Luna", "gpt-5.6-luna",
     "https://opencode.ai/zen/go/v1/responses", "opencode-key", "Authorization",
     API_RESPONSES},
    {"Go / Grok 4.6", "grok-4.6", "https://opencode.ai/zen/go/v1/responses",
     "opencode-key", "Authorization", API_RESPONSES},
    {"Go / DeepSeek V4 Flash", "deepseek-v4-flash",
     "https://opencode.ai/zen/go/v1/chat/completions", "opencode-key",
     "Authorization", API_CHAT_COMPLETIONS}};
#define BACKEND_COUNT (sizeof(backends) / sizeof(*backends))
static inline ApiProtocol backend_protocol(unsigned index) {
    return backends[index].protocol;
}
#endif
