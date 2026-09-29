#include "agent.h"
#include "runtime.h"
#include "tools.h"
#include <math.h>
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *expected;
static unsigned calls;
static bool failed, reported;
static bool right_seen, action_seen, play_reported, freeze_after_done;
static bool fail_inspection_on_a;
typedef struct {
    double x, action, frames;
    bool camera;
} Snapshot;
static Snapshot initial;

static double number(const cJSON *object, const char *name) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(value) ? value->valuedouble : NAN;
}

static bool snapshot(const cJSON *diagnostics, Snapshot *result) {
    if (!diagnostics || cJSON_HasObjectItem(diagnostics, "inspection_error"))
        return false;
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(diagnostics, "state");
    const cJSON *camera = cJSON_GetObjectItemCaseSensitive(state, "camera");
    if (!cJSON_IsObject(state) || (camera && !cJSON_IsObject(camera)))
        return false;
    result->camera = camera != NULL;
    result->x = number(camera ? camera : state, "x");
    result->action = number(state, camera ? "mode" : "y");
    result->frames = number(diagnostics, "frames");
    return isfinite(result->x) && isfinite(result->action) &&
           isfinite(result->frames);
}

void __real_runtime_frame(RuntimeInput);
void __wrap_runtime_frame(RuntimeInput input) {
    while (reported && freeze_after_done)
        cothread_yield_irq(IRQ_VBLANK);
    __real_runtime_frame(input);
    if (!reported || failed || play_reported)
        return;
    cJSON *diagnostics = runtime_inspect();
    if (fail_inspection_on_a && (input.pressed & BUTTON_A)) {
        cJSON_DeleteItemFromObjectCaseSensitive(diagnostics, "state");
        cJSON_AddStringToObject(diagnostics, "inspection_error",
                                "Injected inspection failure");
    }
    Snapshot current;
    if (!snapshot(diagnostics, &current) || current.camera != initial.camera) {
        failed = true;
        fprintf(stderr, "E2E PLAY FAIL inspection unavailable\n");
        cJSON_Delete(diagnostics);
        return;
    }
    if ((input.held & BUTTON_RIGHT) && current.x > initial.x)
        right_seen = true;
    if ((input.pressed & BUTTON_A) &&
        (current.camera ? current.action != initial.action
                        : current.action < initial.action))
        action_seen = true;
    if (right_seen && action_seen && current.frames > initial.frames) {
        play_reported = true;
        fprintf(stderr, "E2E PLAY PASS movement=1 action=1 frames=%.0f\n",
                current.frames);
    }
    cJSON_Delete(diagnostics);
}

bool __real_tools_call(const char *, const cJSON *, char *, unsigned);
bool __wrap_tools_call(const char *name, const cJSON *arguments, char *output,
                       unsigned capacity) {
    if (!expected) {
        FILE *file = fopen("/lutin/expected.json", "rb");
        char data[4096];
        size_t length = file ? fread(data, 1, sizeof(data) - 1, file) : 0;
        if (file)
            fclose(file);
        data[length] = 0;
        expected = cJSON_Parse(data);
    }
    bool ok = __real_tools_call(name, arguments, output, capacity);
    cJSON *item = cJSON_GetArrayItem(expected, calls++);
    bool matched = cJSON_IsBool(item) && cJSON_IsTrue(item) == ok;
    failed |= !matched;
    fprintf(stderr, "E2E TOOL %u %s %s\n", calls, name,
            matched ? "MATCH" : "FAIL");
    if (!ok)
        fprintf(stderr, "E2E TOOL ERROR %s\n", output);
    return ok;
}

void __real_agent_tick(void);
void __wrap_agent_tick(void) {
    static bool initialized;
    if (!initialized) {
        consoleDebugInit(DebugDevice_NOCASH);
        initialized = true;
    }
    __real_agent_tick();
    if (reported && !failed && !runtime_running()) {
        failed = true;
        fprintf(stderr, "E2E PLAY FAIL %s\n", runtime_error());
    }
    if (reported || strcmp(agent_status(), "Agent done"))
        return;
    reported = true;
    cJSON *state = runtime_inspect();
    bool ok = !failed && calls &&
              calls == (unsigned)cJSON_GetArraySize(expected) &&
              runtime_running() && !runtime_testing() && !*runtime_error() &&
              snapshot(state, &initial);
    fprintf(stderr, "E2E DONE %s tools=%u running=%u testing=%u\n",
            ok ? "PASS" : "FAIL", calls, runtime_running(), runtime_testing());
    char *encoded = cJSON_PrintUnformatted(state);
    if (encoded)
        fprintf(stderr, "E2E STATE %s\n", encoded);
    FILE *freeze = fopen("/lutin/freeze-after-done", "rb");
    freeze_after_done = freeze != NULL;
    if (freeze)
        fclose(freeze);
    FILE *inspection = fopen("/lutin/fail-inspection-on-a", "rb");
    fail_inspection_on_a = inspection != NULL;
    if (inspection)
        fclose(inspection);
    free(encoded);
    cJSON_Delete(state);
}
