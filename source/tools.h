#ifndef AI_TOOLS_H
#define AI_TOOLS_H
#include <cJSON.h>
#include <stdbool.h>
cJSON *tools_schema(void);
bool tools_call(const char *name, const cJSON *arguments, char *output,
                unsigned capacity);
bool tools_execute(const char *code, char *output, unsigned capacity);
#endif
