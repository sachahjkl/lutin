#ifndef AI_TOOLS_H
#define AI_TOOLS_H
#include <cJSON.h>
#include <stdbool.h>
cJSON *tools_schema(void);
bool tools_call(const char *name, const cJSON *arguments, char *output,
                unsigned capacity);
bool tools_execute(const char *code, char *output, unsigned capacity);
void tools_set_image_support(bool supported);
const char *tools_capture_result(void);
#endif
