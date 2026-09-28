#ifndef AI_NETWORK_H
#define AI_NETWORK_H
#include <stdbool.h>
typedef void (*NetworkEvent)(const char *data);
bool network_start(const char *body, NetworkEvent event);
void network_set_directory(const char *directory);
void network_set_backend(unsigned index, const char *session_id);
const char *network_wifi(void);
void network_tick(void);
void network_stop(void);
bool network_busy(void);
bool network_output_limited(void);
const char *network_status(void);
#endif
