#ifndef AI_PROTOCOL_H
#define AI_PROTOCOL_H
#include "network.h"
#include <stdbool.h>

typedef enum { API_RESPONSES, API_CHAT_COMPLETIONS } ApiProtocol;
char *protocol_request(const char *body, ApiProtocol protocol);
void protocol_start(ApiProtocol protocol, NetworkEvent callback);
void protocol_event(const char *data);
bool protocol_failed(void);
bool protocol_output_limited(void);
#endif
