#ifndef AI_RESPONSE_ERROR_H
#define AI_RESPONSE_ERROR_H
#include <cJSON.h>
#include <stdio.h>

static void response_error(char *output, size_t capacity, long http,
                           const char *body) {
    cJSON *object = cJSON_Parse(body);
    cJSON *error = cJSON_GetObjectItemCaseSensitive(object, "error");
    cJSON *message = cJSON_GetObjectItemCaseSensitive(error, "message");
    if (!cJSON_IsString(message))
        message = cJSON_GetObjectItemCaseSensitive(object, "detail");
    const char *text = cJSON_IsString(message) ? message->valuestring : body;
    char clean[131];
    size_t i = 0;
    while (text[i] && i < sizeof(clean) - 1) {
        unsigned char value = (unsigned char)text[i];
        clean[i++] = value >= 32 && value < 127 ? (char)value : ' ';
    }
    clean[i] = 0;
    snprintf(output, capacity, "HTTP %ld: %s", http,
             i ? clean : "Backend request failed");
    cJSON_Delete(object);
}
#endif
