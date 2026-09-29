#define _POSIX_C_SOURCE 200809L
#include "agent.h"
#include "backend.h"
#include "font.h"
#include "network.h"
#include "runtime.h"
#include "text.h"
#include "workspace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static cJSON *responses;
static NetworkEvent receiver;
static unsigned requests;

static void record_response(const char *data) {
    cJSON *event = cJSON_Parse(data);
    cJSON *type = cJSON_GetObjectItemCaseSensitive(event, "type");
    if (cJSON_IsString(type) &&
        !strcmp(type->valuestring, "response.completed")) {
        cJSON *response = cJSON_GetObjectItemCaseSensitive(event, "response");
        cJSON *output = cJSON_GetObjectItemCaseSensitive(response, "output");
        if (cJSON_IsArray(output))
            cJSON_AddItemToArray(responses, cJSON_Duplicate(output, true));
    }
    cJSON_Delete(event);
    receiver(data);
}

bool __real_network_start(const char *, NetworkEvent);
bool __wrap_network_start(const char *body, NetworkEvent event) {
    receiver = event;
    fprintf(stderr, "Request %u: image=%s\n", ++requests,
            strstr(body, "input_image") ? "yes" : "no");
    return __real_network_start(body, record_response);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--update-catalog")) {
        network_set_directory(argv[2]);
        if (!network_update_catalog()) {
            fprintf(stderr, "%s\n", network_status());
            return 1;
        }
        time_t started = time(NULL);
        while (network_busy() && time(NULL) - started < 120) {
            network_tick();
            nanosleep(&(struct timespec){.tv_nsec = 100000000}, NULL);
        }
        printf("%s\n", network_status());
        bool updated = network_catalog_updated();
        network_stop();
        return updated ? 0 : 1;
    }
    if (argc != 4) {
        fprintf(stderr, "Usage: live-agent DIRECTORY PROVIDER/MODEL PROMPT\n");
        for (unsigned i = 0; i < BACKEND_COUNT; i++)
            fprintf(stderr, "%s: %s\n", backends[i].id, backends[i].label);
        return 2;
    }
    if (!catalog_load(argv[1])) {
        fprintf(stderr, "%s\n", catalog_error());
        return 2;
    }
    unsigned model = 0;
    while (model < BACKEND_COUNT && strcmp(argv[2], backends[model].id))
        model++;
    if (model >= BACKEND_COUNT)
        return 2;
    workspace_init(true, argv[1]);
    unsigned char font[TEXT_GLYPHS * TEXT_GLYPH_BYTES];
    text_extend_font(font, default_font);
    runtime_set_font(font);
    runtime_set_asset_reader(workspace_read);
    runtime_set_save_writer(workspace_write);
    network_set_directory(argv[1]);
    responses = cJSON_CreateArray();
    if (!agent_open(1) || !agent_select_model((unsigned)model) ||
        !agent_submit(argv[3], false)) {
        fprintf(stderr, "%s\n", agent_status());
        return 1;
    }
    uint16_t pixels[256 * 192] = {0};
    time_t start = time(NULL);
    do {
        agent_tick();
        runtime_frame((RuntimeInput){.pixels = pixels});
        struct timespec delay = {0, 16000000};
        nanosleep(&delay, NULL);
    } while ((agent_busy() || agent_queue_size()) && time(NULL) - start < 900);
    printf("%s\n%s\nRuntime: %s\n%s\n", agent_status(), agent_text(),
           runtime_running() ? "running" : "stopped", runtime_error());
    bool ok = strcmp(agent_status(), "Agent done") == 0 && runtime_running();
    char recording_path[256];
    snprintf(recording_path, sizeof(recording_path), "%s/responses.json",
             argv[1]);
    FILE *recording = fopen(recording_path, "wb");
    char *encoded = cJSON_PrintUnformatted(responses);
    if (recording) {
        if (!encoded ||
            fwrite(encoded, 1, strlen(encoded), recording) != strlen(encoded))
            ok = false;
        if (fclose(recording))
            ok = false;
    } else
        ok = false;
    free(encoded);
    cJSON_Delete(responses);
    if (ok) {
        char path[256];
        snprintf(path, sizeof(path), "%s/result.ppm", argv[1]);
        ok = runtime_capture(path);
    }
    agent_stop();
    runtime_stop();
    return ok ? 0 : 1;
}
