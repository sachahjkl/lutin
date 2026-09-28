#include "config.h"
#include "backend.h"
#include <cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static Config settings = {0, false, false};
static char error[128];
const Config *config_get(void) { return &settings; }
const char *config_error(void) { return error; }

bool config_load(const char *path) {
    settings = (Config){0, false, false};
    error[0] = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT)
            return true;
        snprintf(error, sizeof(error), "Config: %s", strerror(errno));
        return false;
    }
    char data[4097];
    size_t length = fread(data, 1, sizeof(data) - 1, file);
    bool failed = ferror(file) || length == sizeof(data) - 1;
    if (fclose(file) != 0)
        failed = true;
    if (failed) {
        snprintf(error, sizeof(error),
                 "Config: read failed or file exceeds 4095 bytes");
        return false;
    }
    data[length] = 0;
    cJSON *root =
        memchr(data, 0, length) ? NULL : cJSON_ParseWithOpts(data, NULL, true);
    Config candidate = settings;
    unsigned seen = 0;
    if (!cJSON_IsObject(root))
        snprintf(error, sizeof(error), "Config: expected a JSON object");
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        if (error[0])
            break;
        unsigned bit = 0;
        bool valid = false;
        if (!strcmp(item->string, "default_model")) {
            bit = 1;
            if (cJSON_IsString(item)) {
                for (unsigned i = 0; i < BACKEND_COUNT; i++)
                    if (!strcmp(item->valuestring, backends[i].model)) {
                        candidate.default_model = i;
                        valid = true;
                    }
            }
        } else if (!strcmp(item->string, "tool_details")) {
            bit = 2;
            valid = cJSON_IsBool(item);
            candidate.tool_details = cJSON_IsTrue(item);
        } else if (!strcmp(item->string, "startup_view")) {
            bit = 4;
            valid = cJSON_IsString(item) &&
                    (!strcmp(item->valuestring, "keyboard") ||
                     !strcmp(item->valuestring, "sessions"));
            candidate.start_in_sessions =
                valid && !strcmp(item->valuestring, "sessions");
        }
        if (!valid || (seen & bit))
            snprintf(error, sizeof(error),
                     "Config: invalid, unknown or duplicate key %.48s",
                     item->string);
        seen |= bit;
    }
    cJSON_Delete(root);
    if (error[0])
        return false;
    settings = candidate;
    return true;
}
