#ifndef AI_CONFIG_H
#define AI_CONFIG_H
#include <stdbool.h>
typedef struct {
    unsigned default_model;
    bool tool_details;
    bool start_in_sessions;
} Config;
const Config *config_get(void);
bool config_load(const char *path);
const char *config_error(void);
#endif
