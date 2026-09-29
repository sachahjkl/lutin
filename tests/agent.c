#include "agent.h"
#include "backend.h"
#include "network.h"
#include "response_error.h"
#include "runtime.h"
#include "tools.h"
#include "workspace.h"
#include <assert.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static NetworkEvent callback;
static bool busy;
static char *request_body;

bool network_start(const char *body, NetworkEvent event) {
    free(request_body);
    request_body = malloc(strlen(body) + 1);
    strcpy(request_body, body);
    callback = event;
    busy = true;
    return true;
}
void network_tick(void) {}
void network_stop(void) { busy = false; }
bool network_busy(void) { return busy; }
const char *network_status(void) { return "HTTP 200"; }
static bool output_limited;
bool network_output_limited(void) { return output_limited; }

static void complete(const char *code, const char *identifier) {
    cJSON *event = cJSON_CreateObject();
    cJSON_AddStringToObject(event, "type", "response.completed");
    cJSON *response = cJSON_AddObjectToObject(event, "response");
    cJSON *outputs = cJSON_AddArrayToObject(response, "output");
    if (code) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "function_call");
        cJSON_AddStringToObject(item, "name", "execute");
        cJSON_AddStringToObject(item, "call_id", identifier);
        cJSON *arguments = cJSON_CreateObject();
        cJSON_AddStringToObject(arguments, "code", code);
        char *encoded = cJSON_PrintUnformatted(arguments);
        cJSON_AddStringToObject(item, "arguments", encoded);
        free(encoded);
        cJSON_Delete(arguments);
        cJSON_AddItemToArray(outputs, item);
        cJSON *done = cJSON_CreateObject();
        cJSON_AddStringToObject(done, "type", "response.output_item.done");
        cJSON_AddItemToObject(done, "item", cJSON_Duplicate(item, 1));
        char *streamed = cJSON_PrintUnformatted(done);
        callback(streamed);
        free(streamed);
        cJSON_Delete(done);
        cJSON_DeleteItemFromArray(outputs, 0);
    }
    char *data = cJSON_PrintUnformatted(event);
    callback(data);
    free(data);
    cJSON_Delete(event);
    busy = false;
    agent_tick();
}

void network_set_backend(unsigned index, const char *session_id) {
    assert(index < BACKEND_COUNT && session_id && *session_id);
}

static bool native(const char *name, const char *json, char *output,
                   unsigned capacity) {
    cJSON *arguments = cJSON_Parse(json);
    assert(arguments);
    bool ok = tools_call(name, arguments, output, capacity);
    cJSON_Delete(arguments);
    return ok;
}

int main(void) {
    assert(catalog_load("."));
    workspace_init(true, "test-projects");
    runtime_set_asset_reader(workspace_read);
    assert(agent_open(1));
    assert(agent_submit("Create an animation", false));
    agent_tick();
    assert(agent_busy());
    assert(strstr(request_body, "Create an animation"));
    complete("tools.write_file('main.lua','function draw() ds.clear(0xff0000) "
             "end','missing'); return tools.run_program()",
             "call-1");
    assert(runtime_running());
    assert(agent_busy());
    assert(strstr(request_body, "Program started"));
    uint16_t pixels[RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT] = {0};
    runtime_frame((RuntimeInput){.pixels = pixels});
    complete("return tools.capture_screen()", "vision-1");
    assert(strstr(request_body, "input_image") &&
           strstr(request_body, "data:image/png;base64,"));
    char *vision_session = workspace_read("session-1.json", 196608);
    assert(vision_session && strstr(vision_session, "capture-1.png") &&
           !strstr(vision_session, "base64"));
    free(vision_session);
    complete("error('capture must not replay')", "vision-1");
    assert(workspace_missing("capture-2.png"));
    assert(strstr(request_body, "data:image/png;base64,"));
    assert(agent_submit("Next task", false));
    assert(agent_queue_size() == 1);
    complete(NULL, NULL);
    assert(strstr(request_body, "Next task"));
    assert(agent_submit("Steer now", true));
    assert(strstr(request_body, "Steer now"));
    assert(agent_submit("Keep queued", false));
    agent_stop();
    agent_tick();
    assert(!agent_busy());
    assert(agent_queue_size() == 1);
    assert(runtime_running());
    assert(agent_open(1));
    assert(agent_queue_size() == 1);
    assert(agent_edit_queued(0, "Edited request"));
    assert(strcmp(agent_queued(0), "Edited request") == 0);
    assert(agent_submit("Priority request", false));
    assert(agent_prioritize_queued(1));
    assert(strcmp(agent_queued(0), "Priority request") == 0);
    assert(agent_remove_queued(0));
    assert(agent_remove_queued(0));
    assert(agent_queue_size() == 0);
    char result[16384];
    assert(!tools_execute(
        "return tools.write_file('SESSION-1.JSON','oops','missing')", result,
        sizeof(result)));
    assert(
        !tools_execute("return tools.write_file('../token','oops','missing')",
                       result, sizeof(result)));
    assert(
        !tools_execute("return tools.write_file('main.lua','oops','missing')",
                       result, sizeof(result)));
    assert(tools_execute("local t,v=tools.read_file('main.lua'); return "
                         "tools.patch_file('main.lua','ff0000','00ff00',v)",
                         result, sizeof(result)));
    assert(!tools_execute("while true do end", result, sizeof(result)));
    assert(tools_execute("return tools.inspect_runtime()", result,
                         sizeof(result)));
    assert(strstr(result, "\"running\":true"));
    assert(agent_submit("Replay call", true));
    complete("error('must not execute again')", "call-1");
    assert(strstr(request_body, "Program started"));
    agent_stop();
    runtime_stop();
    assert(workspace_write("session-2.json", "{invalid"));
    assert(!agent_open(2));
    assert(agent_submit("Keep previous session", false));
    char *invalid_session = workspace_read("session-2.json", 196608);
    assert(invalid_session && !strcmp(invalid_session, "{invalid"));
    free(invalid_session);
    assert(workspace_write(
        "session-3.json",
        "{\"history\":[{\"type\":\"function_call\",\"name\":\"execute\",\"call_"
        "id\":\"pending\",\"arguments\":\"{}\"}],\"queue\":[],\"journal\":{"
        "\"pending\":{\"result\":\"Interrupted call\"}}}"));
    assert(agent_open(3));
    assert(agent_submit("Recover", true));
    assert(strstr(request_body, "function_call_output"));
    assert(strstr(request_body, "Interrupted call"));
    agent_stop();
    assert(tools_execute(
        "return tostring(math.floor(3.5))..table.concat({'a','b'})", result,
        sizeof(result)));
    assert(!strcmp(result, "3ab"));
    assert(!tools_execute("return tools.write_file('chunk.lua','x',nil)",
                          result, sizeof(result)));
    assert(strstr(result, "expected_version") &&
           workspace_missing("chunk.lua"));
    assert(!native("write_file",
                   "{\"path\":\"chunk.lua\",\"text\":\"bad\",\"expected_"
                   "version\":\"\",\"append\":false}",
                   result, sizeof(result)));
    assert(strstr(result, "'missing'") && workspace_missing("chunk.lua"));
    assert(native("write_file",
                  "{\"path\":\"chunk.lua\",\"text\":\"function "
                  "draw()\",\"expected_version\":\"missing\",\"append\":false}",
                  result, sizeof(result)));
    cJSON *saved = cJSON_Parse(result);
    char revision[16];
    snprintf(revision, sizeof(revision), "%s",
             cJSON_GetObjectItemCaseSensitive(saved, "version")->valuestring);
    cJSON_Delete(saved);
    char append[512];
    snprintf(append, sizeof(append),
             "{\"path\":\"chunk.lua\",\"text\":\" ds.clear(0x112233) "
             "end\",\"expected_version\":\"%s\",\"append\":true}",
             revision);
    assert(native("write_file", append, result, sizeof(result)));
    assert(!native("write_file", append, result, sizeof(result)));
    assert(strstr(result, "version changed"));
    assert(native("read_file",
                  "{\"path\":\"chunk.lua\",\"offset\":0,\"limit\":8}", result,
                  sizeof(result)));
    saved = cJSON_Parse(result);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(saved, "text")->valuestring,
                   "function"));
    assert(cJSON_GetObjectItemCaseSensitive(saved, "next_offset")->valueint ==
           8);
    cJSON_Delete(saved);
    assert(native("run_program", "{\"path\":\"chunk.lua\"}", result,
                  sizeof(result)));
    assert(runtime_running());
    runtime_stop();
    assert(!native("write_file",
                   "{\"path\":\"chunk.lua\",\"text\":\"bad\",\"expected_"
                   "version\":null,\"append\":false}",
                   result, sizeof(result)));
    assert(strstr(result, "expected_version"));
    assert(
        !tools_execute("return string.rep('x',20000)", result, sizeof(result)));
    assert(strstr(result, "do not repeat writes"));
    assert(agent_open(8));
    assert(agent_submit("limited generation", true));
    output_limited = true;
    for (unsigned retry = 0; retry < 2; retry++) {
        busy = false;
        agent_tick();
        assert(agent_busy() && strstr(request_body, "Host recovery"));
    }
    busy = false;
    agent_tick();
    assert(!agent_busy());
    output_limited = false;
    free(request_body);
    request_body = NULL;
    assert(agent_open(4));
    assert(agent_submit("first", false));
    assert(agent_submit("second", false));
    char temporary[192];
    snprintf(temporary, sizeof(temporary), "%s/.write.tmp", workspace_root());
    assert(mkdir(temporary, 0700) == 0);
    agent_tick();
    agent_tick();
    assert(!agent_busy() && agent_queue_size() == 2);
    assert(!agent_submit("unsaved", false));
    assert(!agent_edit_queued(0, "unsaved"));
    assert(!agent_remove_queued(0));
    assert(!agent_prioritize_queued(1));
    assert(agent_queue_size() == 2 && strcmp(agent_queued(0), "first") == 0);
    assert(rmdir(temporary) == 0);
    assert(agent_open(4));
    assert(agent_queue_size() == 2);
    assert(agent_resume());
    agent_tick();
    assert(agent_busy() && agent_queue_size() == 1);
    agent_stop();
    assert(agent_submit("interrupt test", true));
    callback("{\"type\":\"response.output_text.delta\",\"delta\":\"Partial "
             "reply\"}");
    busy = false;
    agent_tick();
    assert(agent_open(4));
    assert(agent_submit("continue", true));
    assert(strstr(request_body, "Incomplete response") &&
           strstr(request_body, "Partial reply"));
    agent_stop();
    assert(workspace_write("notes.txt.tmp", "original"));
    assert(workspace_write("notes.txt", "new"));
    char *original = workspace_read("notes.txt.tmp", 100);
    assert(original && strcmp(original, "original") == 0);
    free(original);
    assert(!workspace_write(".write.tmp", "reserved"));
    assert(workspace_write("replace.txt", "first"));
    assert(workspace_write("replace.txt", "second"));
    char *replacement = workspace_read("replace.txt", 100);
    assert(replacement && strcmp(replacement, "second") == 0);
    free(replacement);
    char target[192], backup[192];
    snprintf(target, sizeof(target), "%s/replace.txt", workspace_root());
    snprintf(backup, sizeof(backup), "%s/.replace.txt.bak", workspace_root());
    assert(rename(target, backup) == 0);
    replacement = workspace_read("replace.txt", 100);
    assert(replacement && strcmp(replacement, "second") == 0);
    free(replacement);
    char *large = malloc(32770);
    memset(large, 'x', 32769);
    large[32769] = 0;
    assert(workspace_write("main.lua", large));
    free(large);
    char entry[32769] = "previous source";
    assert(runtime_start("function update() end"));
    assert(!workspace_load_entry(entry, sizeof(entry), "demo"));
    assert(strcmp(entry, "previous source") == 0 && runtime_running());
    assert(strstr(workspace_error(), "limit"));
    runtime_stop();
    assert(tools_execute("return string.rep('',2147483647)", result,
                         sizeof(result)));
    assert(!tools_execute("return string.rep('x',2147483647)", result,
                          sizeof(result)));
    response_error(result, sizeof(result), 401, "unauthorized\n");
    assert(strstr(result, "unauthorized") && !strchr(result, '\n'));
    response_error(result, sizeof(result), 401,
                   "upstream authentication unavailable\n");
    assert(strstr(result, "upstream authentication unavailable"));
    assert(agent_open(5));
    assert(agent_submit("active", true));
    assert(agent_submit("queued", false));
    callback("{\"type\":\"response.output_text.delta\",\"delta\":\"Retain "
             "across edit\"}");
    assert(mkdir(temporary, 0700) == 0);
    assert(!agent_close());
    assert(strcmp(workspace_root(), "test-projects/projects/1") == 0);
    assert(!agent_open(6));
    assert(rmdir(temporary) == 0);
    assert(agent_edit_queued(0, "edited"));
    agent_tick();
    assert(agent_busy());
    assert(strstr(request_body, "Retain across edit"));
    agent_stop();
    assert(agent_open(6));
    assert(agent_submit("tool save failure", true));
    assert(mkdir(temporary, 0700) == 0);
    complete("return tools.write_file('never.txt','no','missing')",
             "unsaved-call");
    assert(!agent_busy() && workspace_missing("never.txt"));
    assert(rmdir(temporary) == 0);
    assert(agent_submit("resume after failure", true));
    assert(!strstr(request_body, "unsaved-call"));
    agent_stop();
    assert(agent_open(7));
    assert(agent_model() == 0);
    assert(agent_submit("keep when switching", false));
    assert(agent_select_model(3));
    assert(agent_model() == 3 && agent_queue_size() == 1);
    char *saved_model = workspace_read("session-7.json", 1024 * 1024);
    assert(saved_model && strstr(saved_model, "opencode-go/deepseek-v4-flash"));
    free(saved_model);
    assert(agent_open(7) && agent_model() == 3);
    FILE *catalog_file = fopen("models.json", "rb");
    assert(catalog_file);
    char catalog_data[CATALOG_MAX_BYTES];
    size_t catalog_length =
        fread(catalog_data, 1, sizeof(catalog_data) - 1, catalog_file);
    catalog_data[catalog_length] = 0;
    assert(fclose(catalog_file) == 0);
    cJSON *catalog_json = cJSON_Parse(catalog_data);
    assert(catalog_json);
    cJSON_DeleteItemFromArray(
        cJSON_GetObjectItemCaseSensitive(catalog_json, "models"), 3);
    char *reduced_catalog = cJSON_PrintUnformatted(catalog_json);
    cJSON_Delete(catalog_json);
    assert(reduced_catalog && chmod("models.json", 0600) == 0);
    catalog_file = fopen("models.json", "wb");
    assert(catalog_file && fputs(reduced_catalog, catalog_file) >= 0 &&
           fclose(catalog_file) == 0);
    free(reduced_catalog);
    assert(catalog_load(".") && agent_model() == BACKEND_COUNT);
    assert(agent_resume());
    agent_tick();
    assert(agent_queue_size() == 1 && !agent_busy());
    assert(catalog_install(".", catalog_data, catalog_length) &&
           agent_model() == 3);
    assert(mkdir(temporary, 0700) == 0);
    assert(!agent_select_model(0) && agent_model() == 3);
    assert(rmdir(temporary) == 0);
    assert(!agent_select_model(99));
    assert(agent_resume());
    agent_tick();
    assert(strstr(request_body, "deepseek-v4-flash"));
    assert(!strstr(request_body, "opencode-go/deepseek-v4-flash"));
    agent_stop();
    assert(workspace_select(42));
    unsigned created = 0, numbers[4], count;
    for (unsigned i = 1; i <= 15; i++) {
        assert(agent_create(&created) && created == i);
    }
    assert(agent_sessions(10, numbers, 4, &count) && count == 4);
    assert(numbers[0] == 11 && numbers[3] == 14);
    assert(agent_select_model(3));
    assert(agent_submit("discard on reset", false));
    assert(workspace_write("main.lua", "function draw() end"));
    assert(agent_reset());
    assert(agent_queue_size() == 0 && agent_model() == 3);
    assert(agent_open(15) && agent_model() == 3);
    assert(!workspace_missing("main.lua"));
    assert(workspace_write("session-99.json", "invalid"));
    assert(!agent_open(99) && agent_model() == 3);
    assert(agent_submit("retained after failed switch", false));
    assert(agent_open(15) && agent_queue_size() == 1);
    assert(agent_delete());
    assert(workspace_missing("session-15.json"));
    assert(agent_create(&created) && created == 15);
    assert(agent_queue_size() == 0);
    free(request_body);
    puts("Agent: tools, persistence, queue, steer, stop, replay and model "
         "selection passed");
    return 0;
}
