#include "network.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *responses;
static unsigned next_response, delay;
static bool busy;
static NetworkEvent callback;
static char status[96] = "Recorded session ready";

void network_set_directory(const char *directory) { (void)directory; }
void network_set_backend(unsigned index, const char *session_id) {
    (void)index;
    (void)session_id;
}
const char *network_wifi(void) { return "REPLAY"; }
const char *network_status(void) { return status; }
bool network_update_catalog(void) { return false; }
bool network_catalog_updated(void) { return false; }
bool network_busy(void) { return busy; }
bool network_output_limited(void) { return false; }
void network_stop(void) { busy = false; }

bool network_start(const char *body, NetworkEvent event) {
    (void)body;
    if (!responses) {
        FILE *file = fopen("/lutin/replay.json", "rb");
        if (!file)
            return false;
        char *data = malloc(65537);
        if (!data) {
            fclose(file);
            return false;
        }
        size_t length = fread(data, 1, 65536, file);
        bool invalid = ferror(file) || length == 65536;
        fclose(file);
        data[length] = 0;
        responses = invalid ? NULL : cJSON_Parse(data);
        free(data);
    }
    if (!cJSON_IsArray(responses) ||
        next_response >= (unsigned)cJSON_GetArraySize(responses)) {
        snprintf(status, sizeof(status), "Recorded responses exhausted");
        return false;
    }
    callback = event;
    delay = 90;
    busy = true;
    snprintf(status, sizeof(status), "Recorded response %u", next_response + 1);
    return true;
}

void network_tick(void) {
    if (!busy || --delay)
        return;
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "response.completed");
    cJSON *result = cJSON_AddObjectToObject(response, "response");
    cJSON *output = cJSON_Duplicate(
        cJSON_GetArrayItem(responses, (int)next_response++), true);
    cJSON *item;
    cJSON_ArrayForEach(item, output) {
        cJSON *arguments = cJSON_GetObjectItemCaseSensitive(item, "arguments");
        if (cJSON_IsObject(arguments)) {
            char *encoded = cJSON_PrintUnformatted(arguments);
            cJSON_ReplaceItemInObjectCaseSensitive(
                item, "arguments",
                cJSON_CreateString(encoded ? encoded : "{}"));
            free(encoded);
        }
    }
    cJSON_AddItemToObject(result, "output", output);
    char *data = cJSON_PrintUnformatted(response);
    if (data && callback)
        callback(data);
    free(data);
    cJSON_Delete(response);
    busy = false;
}
