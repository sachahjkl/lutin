#define _POSIX_C_SOURCE 200809L
#include "agent.h"
#include "backend.h"
#include "network.h"
#include "runtime.h"
#include "workspace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: live-agent DIRECTORY PROVIDER/MODEL PROMPT\n");
        for (unsigned i = 0; i < BACKEND_COUNT; i++)
            fprintf(stderr, "%s: %s\n", backends[i].id, backends[i].label);
        return 2;
    }
    unsigned model = 0;
    while (model < BACKEND_COUNT && strcmp(argv[2], backends[model].id))
        model++;
    if (model >= BACKEND_COUNT)
        return 2;
    workspace_init(true, argv[1]);
    runtime_set_asset_reader(workspace_read);
    network_set_directory(argv[1]);
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
    if (ok) {
        char path[256];
        snprintf(path, sizeof(path), "%s/result.ppm", argv[1]);
        ok = runtime_capture(path);
    }
    agent_stop();
    runtime_stop();
    return ok ? 0 : 1;
}
