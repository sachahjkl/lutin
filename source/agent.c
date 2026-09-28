#include "agent.h"
#include "backend.h"
#include "config.h"
#include "network.h"
#include "tools.h"
#include "workspace.h"
#include <cJSON.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static cJSON *session;
static cJSON *history;
static cJSON *queue;
static cJSON *outputs;
static cJSON *journal;
static char filename[40];
static char status[160] = "Agent idle";
static char text[2048];
static bool active;
static bool completed;
static bool paused = true;
static unsigned rounds;
static unsigned output_retries;

static const char instructions[] =
    "You are Lutin, an autonomous programming agent on a Nintendo DSi. Use "
    "English "
    "for the UI and your replies. "
    "Create general-purpose interactive Lua programs. Prefer the explicit "
    "read_file, write_file, patch_file, run_program and inspect_runtime tools. "
    "Their JSON schemas describe all arguments. Use execute only to group "
    "operations in short Lua Code Mode scripts. "
    "A file version is an opaque token. For an absent file, expected_version "
    "must be exactly the string 'missing', never ''. For existing files, copy "
    "the returned 8-digit hexadecimal version verbatim. Never invent versions "
    "or assume the result of an operation that has not finished. Read first. "
    "Work in small verified steps. For a game, first create a minimal playable "
    "version, run it, then add one feature per tool call using exact patches. "
    "Keep each execute script below 6000 characters. Use one execute call per "
    "generation (or one explicit tool call). Never print a complete program in "
    "chat. Keep progress and "
    "final replies under 80 words. Save code through tools instead. "
    "For large files, use read_file pages and write_file append=true chunks. "
    "Each write returns a new version for the next chunk. Never run an "
    "incomplete file. "
    "Inside execute, Lua tool functions take positional arguments, NOT JSON "
    "objects or Lua tables. "
    "They return multiple Lua values, NOT objects with fields. Code Mode is a "
    "fresh Lua state for every execute call: local variables and versions do "
    "not survive between calls. The game runs in a separate Lua state. "
    "tools.list_files(offset=0,limit=32) returns project "
    "filenames,next_offset; "
    "tools.read_file(path) returns text,version (nil,'missing' for absent "
    "files). "
    "tools.write_file(path,text,expected_version) saves text; "
    "tools.patch_file(path,old_text,new_text,expected_version) replaces "
    "exactly one match; "
    "Lua write_file and patch_file return status,version as two values. "
    "Inside execute, read the version in that Lua script before writing, or "
    "use the second value from its previous write. For explicit JSON tools, "
    "use version from the preceding read_file, write_file or patch_file "
    "result. "
    "Creation example: local old,v=tools.read_file('main.lua'); "
    "tools.write_file('main.lua', [=[function draw() ds.clear(0x102030) "
    "end]=], v); "
    "tools.run_program('main.lua'); return tools.inspect_runtime(). "
    "Patch example: local old,v=tools.read_file('main.lua'); "
    "assert(old,'Read the project before patching'); "
    "return tools.patch_file('main.lua','0x102030','0x203040',v). "
    "tools.capture_screen() saves capture.ppm and returns its filename, not "
    "model vision; "
    "tools.run_program(path) starts Lua; "
    "tools.stop_program() stops it; tools.inspect_runtime() and "
    "tools.read_logs() return diagnostics. "
    "Return a string from each Code Mode script. Project paths are flat "
    "filenames, at most 80 ASCII characters. "
    "Each file is limited to 32768 bytes. Code Mode has a 512KB memory budget "
    "and 100000 instructions. "
    "Program Lua has 2MB and 100000 instructions per callback. Define init(), "
    "update(dt), draw(). "
    "ds.clear(0xRRGGBB), ds.rect(x,y,width,height,0xRRGGBB) draw on a 256x192 "
    "screen. "
    "ds.text(x,y,text,color) draws ASCII text. ds.line(x1,y1,x2,y2,color) "
    "draws a line. "
    "Generate pixel art and audio as editable Lua asset files through the file "
    "tools. "
    "ds.load_asset('hero.lua') executes a project-local text Lua file and "
    "returns its value; "
    "call only at startup or in init. Each asset is <=32768 bytes, with 65536 "
    "bytes of reads per startup/init. "
    "ds.sprite(rows,palette) compiles pixel art: rows are equal-width strings, "
    "1..64 pixels per dimension; "
    "'.' is transparent, '0123456789abcdef' index 1..16 palette entries of "
    "0xRRGGBB. "
    "Example asset: return ds.sprite({'01','1.'},{0xffffff,0x00ffff}). "
    "ds.draw_sprite(sprite,x,y,scale=1,flip=false) draws it; scale is integer "
    "1..8. "
    "For animation, return a Lua array of sprites and select a frame in "
    "update/draw. "
    "ds.sound('square' or 'noise',notes) compiles 1..128 notes, each "
    "{frequencyHz,durationFrames,volume}. "
    "Frequency is 0 for rest or 32..16000; duration is 1..3600 frames at 60Hz; "
    "volume is 0..127. "
    "ds.play_sound(sound,voice,loop=false) replaces playback on one voice. "
    "Square uses voices 1..3 (default 1); noise uses ds.NOISE_VOICE. "
    "ds.stop_sound(voice) stops it. "
    "Use looping note sequences for music and short square/noise sequences for "
    "effects. "
    "Audio stops when the program stops or fails. Load/compile assets once, "
    "not each frame. "
    "Coordinates must be integers. ds.buttons() returns held,pressed bitmasks. "
    "Use math.floor on moving coordinates before drawing. "
    "Use named button masks: ds.A, ds.B, ds.X, ds.Y, ds.LEFT, ds.RIGHT, ds.UP, "
    "ds.DOWN, ds.L, ds.R. "
    "For example: if pressed & ds.A ~= 0 then ... end. Avoid magic numbers; "
    "name game constants. "
    "ds.touch() returns x,y,down. "
    "math,string,table and basic Lua are available, but no "
    "io,os,package,debug,load,pcall,xpcall. "
    "String pattern functions find,match,gmatch,gsub are unavailable because "
    "native pattern matching bypasses instruction hooks. "
    "Write main.lua, run it, inspect diagnostics, fix errors autonomously. "
    "After a tool error, correct the reported cause before trying again. "
    "Do not call missing APIs. User input reaches the game only in Play "
    "controls. "
    "Do not claim hardware or visual verification unless tool results support "
    "it.";

static const char *string(cJSON *object, const char *name) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(value) ? value->valuestring : "";
}

static bool persist(void) {
    char *data = cJSON_PrintUnformatted(session);
    bool ok = false;
    if (!data)
        snprintf(status, sizeof(status), "Save failed: not enough memory");
    else if (strlen(data) > 196608)
        snprintf(status, sizeof(status), "Save failed: session exceeds 192 KB");
    else {
        ok = workspace_write(filename, data);
        if (!ok)
            snprintf(status, sizeof(status), "Save failed: %s",
                     workspace_error());
    }
    free(data);
    if (!ok) {
        paused = true;
        active = false;
        network_stop();
    }
    return ok;
}

static void bind_session(void) {
    history = cJSON_GetObjectItemCaseSensitive(session, "history");
    queue = cJSON_GetObjectItemCaseSensitive(session, "queue");
    journal = cJSON_GetObjectItemCaseSensitive(session, "journal");
}

static cJSON *transaction(void) {
    cJSON *previous = cJSON_Duplicate(session, 1);
    if (!previous) {
        paused = true;
        active = false;
        network_stop();
        snprintf(status, sizeof(status), "Not enough memory to update session");
    }
    return previous;
}

static bool commit(cJSON *previous) {
    if (persist()) {
        cJSON_Delete(previous);
        return true;
    }
    cJSON_Delete(session);
    session = previous;
    bind_session();
    return false;
}

static bool save_interruption(void) {
    if (!session || !outputs || completed ||
        (!text[0] && !cJSON_GetArraySize(outputs)))
        return true;
    cJSON *previous = transaction();
    if (!previous)
        return false;
    cJSON *interruptions =
        cJSON_GetObjectItemCaseSensitive(session, "interruptions");
    if (!interruptions)
        interruptions = cJSON_AddArrayToObject(session, "interruptions");
    cJSON *record = cJSON_CreateObject();
    cJSON_AddStringToObject(record, "status", "incomplete");
    cJSON_AddStringToObject(record, "text", text);
    cJSON_AddItemToObject(record, "items", cJSON_Duplicate(outputs, 1));
    cJSON_AddItemToArray(interruptions, record);
    if (text[0]) {
        char partial[2100];
        snprintf(partial, sizeof(partial), "[Incomplete response]\n%s", text);
        cJSON *message = cJSON_CreateObject();
        cJSON_AddStringToObject(message, "role", "assistant");
        cJSON_AddStringToObject(message, "content", partial);
        cJSON_AddItemToArray(history, message);
    } else {
        cJSON *item;
        cJSON_ArrayForEach(item, outputs) {
            if (strcmp(string(item, "type"), "message") != 0)
                continue;
            cJSON *message = cJSON_Duplicate(item, 1);
            cJSON_DeleteItemFromObjectCaseSensitive(message, "id");
            cJSON_DeleteItemFromObjectCaseSensitive(message, "status");
            cJSON_AddItemToArray(history, message);
        }
    }
    return commit(previous);
}

void agent_stop(void) {
    network_stop();
    active = false;
    paused = true;
    if (!save_interruption())
        return;
    completed = false;
    cJSON_Delete(outputs);
    outputs = NULL;
}

bool agent_close(void) {
    agent_stop();
    return outputs == NULL;
}

bool agent_resume(void) {
    if (!session || (!active && outputs && !agent_close()))
        return false;
    paused = false;
    snprintf(status, sizeof(status), "Queue resumed");
    return true;
}

static bool open_session(unsigned number) {
    if (!agent_close())
        return false;
    cJSON_Delete(session);
    snprintf(filename, sizeof(filename), "session-%u.json", number);
    char *data = workspace_read(filename, 196608);
    if (!data && !workspace_missing(filename)) {
        session = history = queue = journal = NULL;
        snprintf(status, sizeof(status), "%s", workspace_error());
        return false;
    }
    session = data ? cJSON_Parse(data) : NULL;
    bool invalid = data && !cJSON_IsObject(session);
    free(data);
    if (invalid) {
        cJSON_Delete(session);
        session = history = queue = journal = NULL;
        snprintf(status, sizeof(status),
                 "Invalid session file; preserved on SD");
        return false;
    }
    if (!session)
        session = cJSON_CreateObject();
    if (!cJSON_GetObjectItemCaseSensitive(session, "model"))
        cJSON_AddStringToObject(session, "model",
                                backends[config_get()->default_model].id);
    history = cJSON_GetObjectItemCaseSensitive(session, "history");
    queue = cJSON_GetObjectItemCaseSensitive(session, "queue");
    journal = cJSON_GetObjectItemCaseSensitive(session, "journal");
    if (!history) {
        history = cJSON_CreateArray();
        cJSON_AddItemToObject(session, "history", history);
    }
    if (!queue) {
        queue = cJSON_CreateArray();
        cJSON_AddItemToObject(session, "queue", queue);
    }
    if (!journal) {
        journal = cJSON_CreateObject();
        cJSON_AddItemToObject(session, "journal", journal);
    }
    text[0] = 0;
    snprintf(status, sizeof(status), "Session %u ready", number);
    if (!cJSON_IsArray(history) || !cJSON_IsArray(queue) ||
        !cJSON_IsObject(journal)) {
        cJSON_Delete(session);
        session = history = queue = journal = NULL;
        snprintf(status, sizeof(status),
                 "Invalid session data; preserved on SD");
        return false;
    }
    cJSON *entry;
    cJSON_ArrayForEach(entry, queue) {
        if (!cJSON_IsString(entry)) {
            cJSON_Delete(session);
            session = history = queue = journal = NULL;
            snprintf(status, sizeof(status),
                     "Invalid queue data; preserved on SD");
            return false;
        }
    }
    if (!string(session, "network_id")[0]) {
        char identifier[96];
        static unsigned sequence;
        snprintf(identifier, sizeof(identifier), "lutin-%lx-%lx-%u-%u",
                 (unsigned long)time(NULL), (unsigned long)clock(), number,
                 ++sequence);
        cJSON_AddStringToObject(session, "network_id", identifier);
    }
    bool recovered = false;
    cJSON_ArrayForEach(entry, history) {
        if (strcmp(string(entry, "type"), "function_call") != 0)
            continue;
        const char *identifier = string(entry, "call_id");
        bool found = false;
        cJSON *other;
        cJSON_ArrayForEach(other, history) {
            if (strcmp(string(other, "type"), "function_call_output") == 0 &&
                strcmp(string(other, "call_id"), identifier) == 0)
                found = true;
        }
        if (!found) {
            cJSON *record =
                cJSON_GetObjectItemCaseSensitive(journal, identifier);
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "type", "function_call_output");
            cJSON_AddStringToObject(output, "call_id", identifier);
            cJSON_AddStringToObject(output, "output",
                                    record ? string(record, "result")
                                           : "Interrupted before execution");
            cJSON_AddItemToArray(history, output);
            recovered = true;
        }
    }
    if (recovered)
        return persist();
    return true;
}

bool agent_open(unsigned number) {
    if (!agent_close())
        return false;
    cJSON *previous = session;
    char previous_filename[sizeof(filename)];
    memcpy(previous_filename, filename, sizeof(filename));
    session = NULL;
    if (open_session(number)) {
        cJSON_Delete(previous);
        return true;
    }
    cJSON_Delete(session);
    session = previous;
    memcpy(filename, previous_filename, sizeof(filename));
    bind_session();
    return false;
}

bool agent_sessions(unsigned after, unsigned *numbers, unsigned capacity,
                    unsigned *count) {
    *count = 0;
    DIR *directory = opendir(workspace_root());
    if (!directory) {
        snprintf(status, sizeof(status), "Cannot list sessions");
        return false;
    }
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        unsigned number;
        int end = 0;
        const char *name = entry->d_name;
        if (name[0] == '.')
            name++;
        if (sscanf(name, "session-%u.json%n", &number, &end) != 1 || !end ||
            (name[end] &&
             !(entry->d_name[0] == '.' && !strcmp(name + end, ".bak"))) ||
            !number || number <= after)
            continue;
        unsigned position = 0;
        while (position < *count && numbers[position] < number)
            position++;
        if (position < *count && numbers[position] == number)
            continue;
        if (position >= capacity)
            continue;
        if (*count < capacity)
            (*count)++;
        for (unsigned i = *count - 1; i > position; i--)
            numbers[i] = numbers[i - 1];
        numbers[position] = number;
    }
    closedir(directory);
    return true;
}

bool agent_create(unsigned *number) {
    unsigned candidate = 1, next, count;
    for (;;) {
        if (!agent_sessions(candidate - 1, &next, 1, &count))
            return false;
        if (!count || next != candidate)
            break;
        if (candidate == UINT_MAX) {
            snprintf(status, sizeof(status), "Session identifiers exhausted");
            return false;
        }
        candidate++;
    }
    char path[64];
    snprintf(path, sizeof(path), "session-%u.json", candidate);
    if (!workspace_missing(path) || !workspace_write(path, "{}"))
        return false;
    if (!agent_open(candidate))
        return false;
    *number = candidate;
    return persist();
}

bool agent_reset(void) {
    if (!session || !agent_close())
        return false;
    cJSON *previous = transaction();
    if (!previous)
        return false;
    unsigned model = agent_model();
    cJSON_Delete(session);
    session = cJSON_CreateObject();
    cJSON_AddArrayToObject(session, "history");
    cJSON_AddArrayToObject(session, "queue");
    cJSON_AddObjectToObject(session, "journal");
    cJSON_AddStringToObject(session, "model", backends[model].id);
    char identifier[96];
    snprintf(identifier, sizeof(identifier), "lutin-reset-%lx-%lx",
             (unsigned long)time(NULL), (unsigned long)clock());
    cJSON_AddStringToObject(session, "network_id", identifier);
    bind_session();
    if (!commit(previous))
        return false;
    text[0] = 0;
    snprintf(status, sizeof(status), "Session reset");
    return true;
}

bool agent_delete(void) {
    if (!agent_close() || !workspace_remove(filename)) {
        snprintf(status, sizeof(status), "%s", workspace_error());
        return false;
    }
    cJSON_Delete(session);
    session = history = queue = journal = NULL;
    filename[0] = text[0] = 0;
    snprintf(status, sizeof(status), "Session deleted");
    return true;
}

static void event(const char *data) {
    cJSON *object = cJSON_Parse(data);
    if (!object)
        return;
    const char *type = string(object, "type");
    if (strcmp(type, "response.output_text.delta") == 0) {
        const char *delta = string(object, "delta");
        size_t length = strlen(text);
        snprintf(text + length, sizeof(text) - length, "%s", delta);
    }
    if (strcmp(type, "response.completed") == 0) {
        cJSON *response = cJSON_GetObjectItemCaseSensitive(object, "response");
        cJSON *final = cJSON_GetObjectItemCaseSensitive(response, "output");
        if (cJSON_IsArray(final) && cJSON_GetArraySize(final)) {
            cJSON_Delete(outputs);
            outputs = cJSON_Duplicate(final, 1);
        }
        completed = cJSON_IsArray(outputs);
    }
    if (strcmp(type, "response.output_item.done") == 0) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(object, "item");
        if (cJSON_IsObject(item))
            cJSON_AddItemToArray(outputs, cJSON_Duplicate(item, 1));
    }
    if (strcmp(type, "response.failed") == 0 || strcmp(type, "error") == 0)
        snprintf(status, sizeof(status), "Backend returned an error");
    cJSON_Delete(object);
}

static bool request(void) {
    if (outputs && !completed && !save_interruption())
        return false;
    cJSON *body = cJSON_CreateObject();
    unsigned model = agent_model();
    network_set_backend(model, string(session, "network_id"));
    cJSON_AddStringToObject(body, "model", backends[model].model);
    cJSON_AddNumberToObject(body, "max_output_tokens", 4096);
    cJSON_AddStringToObject(body, "instructions", instructions);
    cJSON_AddBoolToObject(body, "stream", true);
    cJSON_AddBoolToObject(body, "store", false);
    cJSON_AddItemToObject(body, "include",
                          cJSON_Parse("[\"reasoning.encrypted_content\"]"));
    cJSON_AddItemToObject(body, "input", cJSON_Duplicate(history, 1));
    cJSON *tools = tools_schema();
    cJSON_AddItemToObject(body, "tools", tools);
    cJSON_AddBoolToObject(body, "parallel_tool_calls", false);
    char *encoded = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    text[0] = 0;
    completed = false;
    cJSON_Delete(outputs);
    outputs = cJSON_CreateArray();
    bool ok = encoded && network_start(encoded, event);
    free(encoded);
    if (!ok)
        snprintf(status, sizeof(status), "%s", network_status());
    if (!ok)
        paused = true;
    return ok;
}

static void user_message(const char *value) {
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", value);
    cJSON_AddItemToArray(history, message);
}

bool agent_submit(const char *value, bool steer) {
    if (!session || !value[0])
        return false;
    if (steer) {
        agent_stop();
        if (outputs)
            return false;
        cJSON *previous = transaction();
        if (!previous)
            return false;
        user_message(value);
        rounds = 0;
        output_retries = 0;
        paused = false;
        if (!commit(previous))
            return false;
        active = request();
        return active;
    }
    if (cJSON_GetArraySize(queue) >= 16) {
        snprintf(status, sizeof(status), "Queue full (16 requests)");
        return false;
    }
    if (!active && outputs) {
        agent_stop();
        if (outputs)
            return false;
    }
    cJSON *previous = transaction();
    if (!previous)
        return false;
    cJSON_AddItemToArray(queue, cJSON_CreateString(value));
    paused = false;
    return commit(previous);
}

void agent_tick(void) {
    network_tick();
    if (network_busy()) {
        snprintf(status, sizeof(status), "%s", network_status());
        return;
    }
    if (active) {
        if (!completed) {
            if (network_output_limited() && output_retries < 2 && rounds < 31) {
                if (!agent_close())
                    return;
                cJSON *previous = transaction();
                if (!previous)
                    return;
                user_message(
                    "[Host recovery] The last generation exceeded its output "
                    "budget. No tool from that incomplete generation was "
                    "executed. Continue the original task with one small tool "
                    "call. Use paged reads, write_file chunks with append=true "
                    "and the returned version, or a small exact patch. Do not "
                    "repeat a full file or long explanation.");
                if (!commit(previous))
                    return;
                output_retries++;
                rounds++;
                paused = false;
                active = request();
                return;
            }
            snprintf(status, sizeof(status), "Request failed: %.130s",
                     network_status());
            agent_stop();
            return;
        }
        bool called = false;
        cJSON *item;
        cJSON_ArrayForEach(item, outputs) {
            cJSON *previous = transaction();
            if (!previous) {
                agent_stop();
                return;
            }
            cJSON_AddItemToArray(history, cJSON_Duplicate(item, 1));
            if (strcmp(string(item, "type"), "function_call") != 0) {
                cJSON_Delete(previous);
                continue;
            }
            char result[16384];
            cJSON *arguments = cJSON_Parse(string(item, "arguments"));
            const char *call_id = string(item, "call_id");
            cJSON *record = cJSON_GetObjectItemCaseSensitive(journal, call_id);
            if (record) {
                snprintf(result, sizeof(result), "%s",
                         string(record, "result"));
            } else if (call_id[0] &&
                       strlen(string(item, "arguments")) <= 65536) {
                record = cJSON_CreateObject();
                cJSON_AddStringToObject(record, "name", string(item, "name"));
                cJSON_AddItemToObject(record, "arguments",
                                      cJSON_Duplicate(arguments, 1));
                cJSON_AddStringToObject(record, "result",
                                        "Interrupted call: inspect files and "
                                        "runtime before continuing");
                cJSON_AddItemToObject(journal, call_id, record);
                if (!commit(previous)) {
                    cJSON_Delete(arguments);
                    agent_stop();
                    return;
                }
                previous = NULL;
                bool ok = tools_call(string(item, "name"), arguments, result,
                                     sizeof(result));
                cJSON_AddBoolToObject(record, "ok", ok);
                cJSON_ReplaceItemInObjectCaseSensitive(
                    record, "result", cJSON_CreateString(result));
            } else
                snprintf(result, sizeof(result), "Invalid tool call");
            cJSON_Delete(previous);
            cJSON_Delete(arguments);
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "type", "function_call_output");
            cJSON_AddStringToObject(output, "call_id", call_id);
            cJSON_AddStringToObject(output, "output", result);
            cJSON_AddItemToArray(history, output);
            if (!persist()) {
                agent_stop();
                return;
            }
            called = true;
        }
        cJSON_Delete(outputs);
        outputs = NULL;
        if (!persist()) {
            agent_stop();
            return;
        }
        if (called && ++rounds < 32) {
            active = request();
            return;
        }
        active = false;
        snprintf(status, sizeof(status),
                 called ? "Turn limit reached" : "Agent done");
    }
    if (!paused && queue && cJSON_GetArraySize(queue)) {
        cJSON *previous = transaction();
        if (!previous)
            return;
        cJSON *next = cJSON_DetachItemFromArray(queue, 0);
        user_message(next->valuestring);
        cJSON_Delete(next);
        rounds = 0;
        output_retries = 0;
        if (commit(previous))
            active = request();
    }
}

bool agent_busy(void) { return active || network_busy(); }

unsigned agent_model(void) {
    const char *model = string(session, "model");
    for (unsigned i = 0; i < BACKEND_COUNT; i++)
        if (!strcmp(model, backends[i].id))
            return i;
    return config_get()->default_model;
}

bool agent_select_model(unsigned index) {
    if (!session || index >= BACKEND_COUNT || !agent_close())
        return false;
    cJSON *previous = transaction();
    if (!previous)
        return false;
    cJSON_DeleteItemFromObjectCaseSensitive(session, "model");
    cJSON_AddStringToObject(session, "model", backends[index].id);
    /* Encrypted reasoning cannot be transferred between providers. */
    for (int i = cJSON_GetArraySize(history) - 1; i >= 0; i--) {
        cJSON *item = cJSON_GetArrayItem(history, i);
        if (!strcmp(string(item, "type"), "reasoning"))
            cJSON_DeleteItemFromArray(history, i);
        else {
            cJSON_DeleteItemFromObjectCaseSensitive(item, "id");
            cJSON_DeleteItemFromObjectCaseSensitive(item, "status");
        }
    }
    if (!commit(previous))
        return false;
    snprintf(status, sizeof(status), "Selected %s", backends[index].label);
    return true;
}

void agent_conversation(AgentConversationText visit, void *context) {
    cJSON *item;
    cJSON_ArrayForEach(item, history) {
        const char *type = string(item, "type");
        if (strcmp(type, "function_call") == 0) {
            char label[33];
            snprintf(label, sizeof(label), "TOOL / %.24s",
                     string(item, "name"));
            visit(label, string(item, "arguments"), context);
        } else if (strcmp(type, "function_call_output") == 0) {
            cJSON *record = cJSON_GetObjectItemCaseSensitive(
                journal, string(item, "call_id"));
            cJSON *ok = cJSON_GetObjectItemCaseSensitive(record, "ok");
            visit(cJSON_IsFalse(ok) ? "TOOL / error" : "TOOL / result",
                  string(item, "output"), context);
        } else if (strcmp(string(item, "role"), "user") == 0 ||
                   strcmp(string(item, "role"), "assistant") == 0) {
            const char *role =
                strcmp(string(item, "role"), "user") == 0 ? "YOU" : "AGENT";
            cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
            if (cJSON_IsString(content))
                visit(role, content->valuestring, context);
            else {
                cJSON *part;
                cJSON_ArrayForEach(part, content) {
                    const char *value = string(part, "text");
                    if (*value)
                        visit(role, value, context);
                }
            }
        }
    }
    if (active && text[0])
        visit("AGENT / receiving", text, context);
}
const char *agent_status(void) { return status; }
const char *agent_text(void) { return text; }
unsigned agent_queue_size(void) {
    return queue ? (unsigned)cJSON_GetArraySize(queue) : 0;
}
bool agent_remove_queued(unsigned index) {
    if (index >= agent_queue_size())
        return false;
    cJSON *previous = transaction();
    if (!previous)
        return false;
    cJSON_DeleteItemFromArray(queue, (int)index);
    return commit(previous);
}

const char *agent_queued(unsigned index) {
    cJSON *item = queue ? cJSON_GetArrayItem(queue, (int)index) : NULL;
    return cJSON_IsString(item) ? item->valuestring : "";
}

bool agent_edit_queued(unsigned index, const char *value) {
    if (index >= agent_queue_size() || !value[0])
        return false;
    if (!active && outputs && !agent_close())
        return false;
    cJSON *previous = transaction();
    if (!previous)
        return false;
    cJSON_ReplaceItemInArray(queue, (int)index, cJSON_CreateString(value));
    paused = false;
    return commit(previous);
}

bool agent_prioritize_queued(unsigned index) {
    if (index >= agent_queue_size())
        return false;
    cJSON *previous = transaction();
    if (!previous)
        return false;
    cJSON *item = cJSON_DetachItemFromArray(queue, (int)index);
    cJSON_InsertItemInArray(queue, 0, item);
    return commit(previous);
}
