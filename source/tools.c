#include "tools.h"
#include "runtime.h"
#include "sandbox.h"
#include "workspace.h"
#include <ctype.h>
#include <dirent.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t used;
    unsigned instructions;
    char *temporary;
    unsigned operations;
} Budget;

static Budget *context(lua_State *state) {
    Budget *budget;
    lua_getallocf(state, (void **)&budget);
    return budget;
}

static void operation(lua_State *state) {
    if (++context(state)->operations > 32)
        luaL_error(state, "Code Mode tool limit exceeded (32 calls)");
}

static bool session_file(const char *path) {
    const char prefix[] = "session-";
    for (unsigned i = 0; i < sizeof(prefix) - 1; i++)
        if (!path[i] || tolower((unsigned char)path[i]) != prefix[i])
            return false;
    return true;
}

static void version(const char *data, char output[16]) {
    if (!data) {
        snprintf(output, 16, "missing");
        return;
    }
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)data; *p; p++)
        hash = (hash ^ *p) * 16777619u;
    snprintf(output, 16, "%08lx", (unsigned long)hash);
}

static void *allocate(void *context, void *pointer, size_t old, size_t size) {
    Budget *budget = context;
    if (!pointer)
        old = 0;
    if (!size) {
        free(pointer);
        budget->used -= old;
        return NULL;
    }
    if (size > 512 * 1024 - (budget->used - old))
        return NULL;
    void *result = realloc(pointer, size);
    if (result)
        budget->used = budget->used - old + size;
    return result;
}

static void hook(lua_State *state, lua_Debug *debug) {
    (void)debug;
    Budget *budget;
    lua_getallocf(state, (void **)&budget);
    if (++budget->instructions >= 100)
        luaL_error(state, "Code Mode instruction budget exceeded");
}

static int read_file(lua_State *state) {
    operation(state);
    const char *path = luaL_checkstring(state, 1);
    char *data = workspace_read(path, 32768);
    char revision[16];
    version(data, revision);
    if (!data) {
        if (!workspace_missing(path))
            return luaL_error(state, "%s", workspace_error());
        lua_pushnil(state);
        lua_pushstring(state, revision);
        return 2;
    }
    context(state)->temporary = data;
    lua_pushstring(state, data);
    free(data);
    context(state)->temporary = NULL;
    lua_pushstring(state, revision);
    return 2;
}

static const char *required_string(lua_State *state, int index,
                                   const char *signature) {
    if (lua_type(state, index) != LUA_TSTRING)
        luaL_error(
            state,
            "%s: argument %d must be a string, got %s. Use positional "
            "arguments, not a table. Get the version with local "
            "text,version=tools.read_file(path) in this same execute call.",
            signature, index, luaL_typename(state, index));
    return lua_tostring(state, index);
}

static int write_file(lua_State *state) {
    operation(state);
    const char *signature = "write_file(path,text,expected_version)";
    const char *path = required_string(state, 1, signature);
    required_string(state, 2, signature);
    size_t length;
    const char *data = luaL_checklstring(state, 2, &length);
    const char *expected = required_string(state, 3, signature);
    if (!lua_isnoneornil(state, 4) && !lua_isboolean(state, 4))
        return luaL_error(state,
                          "write_file append argument must be a boolean");
    if (length > 32768 || strlen(data) != length)
        return luaL_error(state, "Text file must be at most 32768 bytes");
    if (session_file(path))
        return luaL_error(state, "Session files are managed by the host");
    char *previous = workspace_read(path, 32768);
    if (!previous && !workspace_missing(path))
        return luaL_error(state, "%s", workspace_error());
    char revision[16];
    version(previous, revision);
    if (strcmp(expected, revision) != 0) {
        free(previous);
        if (!strcmp(revision, "missing"))
            return luaL_error(
                state,
                "File does not exist. Set expected_version to the exact string "
                "'missing', not an empty string or an invented token.");
        return luaL_error(state, "File version changed; read the file again");
    }
    if (lua_toboolean(state, 4)) {
        size_t old_length = previous ? strlen(previous) : 0;
        if (old_length + length > 32768) {
            free(previous);
            return luaL_error(state, "Append exceeds 32768-byte file limit");
        }
        char *joined = realloc(previous, old_length + length + 1);
        if (!joined) {
            free(previous);
            return luaL_error(state, "Not enough memory to append");
        }
        memcpy(joined + old_length, data, length + 1);
        context(state)->temporary = joined;
        data = joined;
    } else
        free(previous);
    if (!workspace_write(path, data))
        return luaL_error(state, "%s", workspace_error());
    version(data, revision);
    free(context(state)->temporary);
    context(state)->temporary = NULL;
    lua_pushliteral(state, "File saved");
    lua_pushstring(state, revision);
    return 2;
}

static int patch_file(lua_State *state) {
    operation(state);
    const char *signature =
        "patch_file(path,old_text,new_text,expected_version)";
    const char *path = required_string(state, 1, signature);
    const char *old = required_string(state, 2, signature);
    const char *replacement = required_string(state, 3, signature);
    const char *expected = required_string(state, 4, signature);
    char *data = workspace_read(path, 32768);
    if (!data)
        return luaL_error(state, "%s", workspace_error());
    char *match = old[0] ? strstr(data, old) : NULL;
    if (!match || strstr(match + strlen(old), old)) {
        free(data);
        return luaL_error(state,
                          "Patch requires exactly one matching text block");
    }
    size_t size = strlen(data) - strlen(old) + strlen(replacement);
    char *updated = size <= 32768 ? malloc(size + 1) : NULL;
    if (!updated) {
        free(data);
        return luaL_error(state, "Patch exceeds file or memory limit");
    }
    size_t prefix = (size_t)(match - data);
    memcpy(updated, data, prefix);
    strcpy(updated + prefix, replacement);
    strcpy(updated + prefix + strlen(replacement), match + strlen(old));
    free(data);
    context(state)->temporary = updated;
    lua_pushcfunction(state, write_file);
    lua_pushstring(state, path);
    lua_pushstring(state, updated);
    free(updated);
    context(state)->temporary = NULL;
    lua_pushstring(state, expected);
    lua_call(state, 3, 2);
    return 2;
}

static int run_program(lua_State *state) {
    operation(state);
    const char *path = luaL_optstring(state, 1, "main.lua");
    char *data = workspace_read(path, 32768);
    if (!data)
        return luaL_error(state, "%s", workspace_error());
    bool ok = runtime_start(data);
    free(data);
    if (!ok)
        return luaL_error(state, "%s", runtime_error());
    lua_pushliteral(state, "Program started");
    return 1;
}

static int stop_program(lua_State *state) {
    operation(state);
    runtime_stop();
    lua_pushliteral(state, "Program stopped");
    return 1;
}

static int inspect_runtime(lua_State *state) {
    operation(state);
    lua_pushfstring(state, "running=%s memory=%d error=%s",
                    runtime_running() ? "true" : "false", (int)runtime_memory(),
                    runtime_error());
    return 1;
}

static int capture_screen(lua_State *state) {
    operation(state);
    char path[160];
    if (!workspace_path("capture.ppm", path, sizeof(path)) ||
        !runtime_capture(path))
        return luaL_error(state, "Screen capture failed");
    lua_pushliteral(state, "capture.ppm (256x192 RGB, PPM)");
    return 1;
}

static int read_logs(lua_State *state) {
    operation(state);
    lua_Integer cursor = luaL_optinteger(state, 1, 0);
    if (cursor < 0)
        return luaL_error(state, "Log cursor must be nonnegative");
    char output[8192];
    unsigned next = runtime_logs((unsigned)cursor, output, sizeof(output));
    lua_pushstring(state, output);
    lua_pushinteger(state, next);
    return 2;
}

static int list_files(lua_State *state) {
    operation(state);
    int offset = (int)luaL_optinteger(state, 1, 0);
    int limit = (int)luaL_optinteger(state, 2, 32);
    if (offset < 0 || offset > 10000 || limit < 1 || limit > 32)
        return luaL_error(state, "Invalid file page (limit 1..32)");
    DIR *directory = opendir(workspace_root());
    if (!directory)
        return luaL_error(state, "Project directory unavailable");
    char result[4096] = "";
    size_t length = 0;
    struct dirent *entry;
    int index = 0, count = 0;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.' || session_file(entry->d_name))
            continue;
        if (index++ < offset)
            continue;
        if (count++ >= limit)
            break;
        size_t size = strlen(entry->d_name);
        if (length + size + 2 >= sizeof(result))
            break;
        memcpy(result + length, entry->d_name, size);
        length += size;
        result[length++] = '\n';
        result[length] = 0;
    }
    closedir(directory);
    lua_pushstring(state, result);
    lua_pushinteger(state, offset + (count > limit ? limit : count));
    return 2;
}

static int read_page(lua_State *state) {
    operation(state);
    const char *path = luaL_checkstring(state, 1);
    lua_Integer offset = luaL_checkinteger(state, 2);
    lua_Integer limit = luaL_checkinteger(state, 3);
    if (offset < 0 || offset > 32768 || limit < 1 || limit > 4096)
        return luaL_error(
            state, "Read offset must be 0..32768 and limit 1..4096 bytes");
    char *data = workspace_read(path, 32768);
    if (!data && !workspace_missing(path))
        return luaL_error(state, "%s", workspace_error());
    context(state)->temporary = data;
    char revision[16];
    version(data, revision);
    size_t length = data ? strlen(data) : 0;
    size_t start = (size_t)offset < length ? (size_t)offset : length;
    size_t count = length - start;
    if (count > (size_t)limit)
        count = (size_t)limit;
    if (data)
        lua_pushlstring(state, data + start, count);
    else
        lua_pushnil(state);
    lua_pushstring(state, revision);
    if (start + count < length)
        lua_pushinteger(state, (lua_Integer)(start + count));
    else
        lua_pushnil(state);
    lua_pushinteger(state, (lua_Integer)length);
    free(data);
    context(state)->temporary = NULL;
    return 4;
}

typedef enum { ARG_TEXT, ARG_INTEGER, ARG_BOOLEAN } ArgumentType;
typedef struct {
    const char *name;
    ArgumentType type;
} Argument;
typedef struct {
    const char *name, *description;
    lua_CFunction function;
    Argument arguments[4];
    const char *results[4];
} NativeTool;

static const NativeTool native_tools[] = {
    {"read_file",
     "Read a file page. offset is a byte offset, limit is 1..4096. Returns "
     "text, full-file version, next_offset (null at end), total_bytes. Missing "
     "files return text=null, version=missing.",
     read_page,
     {{"path", ARG_TEXT}, {"offset", ARG_INTEGER}, {"limit", ARG_INTEGER}},
     {"text", "version", "next_offset", "total_bytes"}},
    {"write_file",
     "Write at most 8192 bytes per call. Use append=false to replace, "
     "append=true to add a chunk. Supply the current expected_version from "
     "read_file or the previous write. Returns the new version. File limit: "
     "32768 bytes. Run only after the last chunk.",
     write_file,
     {{"path", ARG_TEXT},
      {"text", ARG_TEXT},
      {"expected_version", ARG_TEXT},
      {"append", ARG_BOOLEAN}},
     {"message", "version"}},
    {"patch_file",
     "Replace exactly one occurrence of old_text with new_text. This is an "
     "exact-text edit, NOT a unified diff. Each text block is at most 8192 "
     "bytes. Supply the current expected_version. Returns the new version.",
     patch_file,
     {{"path", ARG_TEXT},
      {"old_text", ARG_TEXT},
      {"new_text", ARG_TEXT},
      {"expected_version", ARG_TEXT}},
     {"message", "version"}},
    {"run_program",
     "Load and run a complete Lua program from the project. Do not run between "
     "write chunks.",
     run_program,
     {{"path", ARG_TEXT}},
     {"message"}},
    {"inspect_runtime",
     "Get running state, memory and the last runtime error.",
     inspect_runtime,
     {{0}},
     {"diagnostics"}},
    {"read_logs",
     "Read logs from cursor=0 or the previous next_cursor. Returns logs and "
     "next_cursor.",
     read_logs,
     {{"cursor", ARG_INTEGER}},
     {"logs", "next_cursor"}},
    {"list_files",
     "List project filenames. offset=0 for the first page, limit=1..32. "
     "Returns filenames and next_offset.",
     list_files,
     {{"offset", ARG_INTEGER}, {"limit", ARG_INTEGER}},
     {"filenames", "next_offset"}},
    {"execute",
     "Run a short Lua Code Mode script for grouped operations. Prefer the "
     "explicit file tools for ordinary edits. Return a short string, not a "
     "complete file.",
     NULL,
     {{"code", ARG_TEXT}},
     {"result"}}};

cJSON *tools_schema(void) {
    cJSON *schema = cJSON_CreateArray();
    for (unsigned i = 0; i < sizeof(native_tools) / sizeof(*native_tools);
         i++) {
        const NativeTool *tool = &native_tools[i];
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "type", "function");
        cJSON_AddStringToObject(entry, "name", tool->name);
        cJSON_AddStringToObject(entry, "description", tool->description);
        cJSON_AddBoolToObject(entry, "strict", true);
        cJSON *parameters = cJSON_AddObjectToObject(entry, "parameters");
        cJSON_AddStringToObject(parameters, "type", "object");
        cJSON_AddBoolToObject(parameters, "additionalProperties", false);
        cJSON *properties = cJSON_AddObjectToObject(parameters, "properties");
        cJSON *required = cJSON_AddArrayToObject(parameters, "required");
        for (unsigned j = 0; j < 4 && tool->arguments[j].name; j++) {
            const Argument *argument = &tool->arguments[j];
            cJSON *property =
                cJSON_AddObjectToObject(properties, argument->name);
            cJSON_AddStringToObject(property, "type",
                                    argument->type == ARG_TEXT ? "string"
                                    : argument->type == ARG_INTEGER
                                        ? "integer"
                                        : "boolean");
            if (argument->type == ARG_TEXT)
                cJSON_AddNumberToObject(property, "maxLength",
                                        !strcmp(argument->name, "code") ? 32768
                                                                        : 8192);
            if (!strcmp(argument->name, "expected_version")) {
                cJSON_AddStringToObject(
                    property, "description",
                    "Copy the version string returned by read_file or the "
                    "previous write/patch verbatim. For an absent file use "
                    "exactly missing. Never invent a version or use an empty "
                    "string.");
                cJSON_AddStringToObject(property, "pattern",
                                        "^(missing|[0-9a-f]{8})$");
            }
            cJSON_AddItemToArray(required, cJSON_CreateString(argument->name));
        }
        cJSON_AddItemToArray(schema, entry);
    }
    return schema;
}

typedef struct {
    const NativeTool *tool;
    const cJSON *arguments;
} NativeCall;

static int invoke_native(lua_State *state) {
    NativeCall *call = lua_touserdata(state, 1);
    lua_pushcfunction(state, call->tool->function);
    unsigned count = 0;
    while (count < 4 && call->tool->arguments[count].name) {
        const Argument *argument = &call->tool->arguments[count];
        const cJSON *value =
            cJSON_GetObjectItemCaseSensitive(call->arguments, argument->name);
        if (argument->type == ARG_TEXT && cJSON_IsString(value) &&
            strlen(value->valuestring) <= 8192)
            lua_pushstring(state, value->valuestring);
        else if (argument->type == ARG_INTEGER && cJSON_IsNumber(value) &&
                 value->valuedouble >= 0 && value->valuedouble <= 2147483647 &&
                 value->valuedouble == value->valueint)
            lua_pushinteger(state, value->valueint);
        else if (argument->type == ARG_BOOLEAN && cJSON_IsBool(value))
            lua_pushboolean(state, cJSON_IsTrue(value));
        else
            return luaL_error(
                state,
                "%s: invalid or missing argument '%s'. Text limit is 8192 "
                "bytes; split large writes into versioned chunks.",
                call->tool->name, argument->name);
        count++;
    }
    lua_call(state, (int)count, LUA_MULTRET);
    return lua_gettop(state) - 1;
}

bool tools_call(const char *name, const cJSON *arguments, char *output,
                unsigned capacity) {
    const NativeTool *tool = NULL;
    for (unsigned i = 0; i < sizeof(native_tools) / sizeof(*native_tools); i++)
        if (!strcmp(name, native_tools[i].name))
            tool = &native_tools[i];
    if (!tool || !cJSON_IsObject(arguments)) {
        snprintf(output, capacity, "Unknown tool or invalid JSON arguments");
        return false;
    }
    if (!tool->function) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(arguments, "code");
        if (!cJSON_IsString(code) || strlen(code->valuestring) > 32768) {
            snprintf(output, capacity,
                     "execute requires a code string of at most 32768 bytes");
            return false;
        }
        return tools_execute(code->valuestring, output, capacity);
    }
    Budget budget = {0};
    lua_State *state = lua_newstate(allocate, &budget);
    if (!state) {
        snprintf(output, capacity, "Tool memory unavailable");
        return false;
    }
    NativeCall call = {tool, arguments};
    lua_pushcfunction(state, invoke_native);
    lua_pushlightuserdata(state, &call);
    bool ok = lua_pcall(state, 1, LUA_MULTRET, 0) == LUA_OK;
    if (ok) {
        cJSON *result = cJSON_CreateObject();
        for (int i = 1;
             i <= lua_gettop(state) && i <= 4 && tool->results[i - 1]; i++) {
            const char *key = tool->results[i - 1];
            if (lua_type(state, i) == LUA_TSTRING)
                cJSON_AddStringToObject(result, key, lua_tostring(state, i));
            else if (lua_isinteger(state, i))
                cJSON_AddNumberToObject(result, key,
                                        (double)lua_tointeger(state, i));
            else
                cJSON_AddNullToObject(result, key);
        }
        char *encoded = cJSON_PrintUnformatted(result);
        ok = encoded && strlen(encoded) < capacity;
        snprintf(
            output, capacity, "%s",
            ok ? encoded
               : "Tool result exceeds output budget; request a smaller page");
        free(encoded);
        cJSON_Delete(result);
    } else
        snprintf(output, capacity, "%s",
                 lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1)
                                                    : "Non-text tool error");
    lua_close(state);
    free(budget.temporary);
    return ok;
}

static int initialize(lua_State *state) {
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "math", luaopen_math, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "table", luaopen_table, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "string", luaopen_string, 1);
    sandbox_limit_string(state);
    const char *patterns[] = {"find", "match", "gmatch", "gsub"};
    for (unsigned i = 0; i < sizeof(patterns) / sizeof(*patterns); i++) {
        lua_pushnil(state);
        lua_setfield(state, -2, patterns[i]);
    }
    lua_pop(state, 1);
    const char *removed[] = {"dofile", "loadfile", "load",          "pcall",
                             "xpcall", "print",    "collectgarbage"};
    for (unsigned i = 0; i < sizeof(removed) / sizeof(*removed); i++) {
        lua_pushnil(state);
        lua_setglobal(state, removed[i]);
    }
    const luaL_Reg functions[] = {
        {"read_file", read_file},       {"write_file", write_file},
        {"patch_file", patch_file},     {"capture_screen", capture_screen},
        {"list_files", list_files},     {"run_program", run_program},
        {"stop_program", stop_program}, {"inspect_runtime", inspect_runtime},
        {"read_logs", read_logs},       {NULL, NULL}};
    luaL_newlib(state, functions);
    lua_setglobal(state, "tools");
    return 0;
}

bool tools_execute(const char *code, char *output, unsigned capacity) {
    Budget budget = {0};
    lua_State *state = lua_newstate(allocate, &budget);
    if (!state) {
        snprintf(output, capacity, "Code Mode memory unavailable");
        return false;
    }
    lua_sethook(state, hook, LUA_MASKCOUNT, 1000);
    lua_pushcfunction(state, initialize);
    int status = lua_pcall(state, 0, 0, 0);
    if (status == LUA_OK)
        status = luaL_loadbufferx(state, code, strlen(code), "code-mode", "t");
    if (status == LUA_OK)
        status = lua_pcall(state, 0, 1, 0);
    const char *result =
        lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : NULL;
    snprintf(output, capacity, "%s",
             result
                 ? result
                 : (status == LUA_OK ? "Done (return a string for tool output)"
                                     : "Non-text Lua error"));
    if (result && strlen(result) >= capacity) {
        snprintf(output, capacity,
                 "Tool result exceeds output budget. Operations already "
                 "completed; do not repeat writes. Use the explicit read_file "
                 "tool with smaller pages, or return a short summary.");
        status = LUA_ERRRUN;
    }
    lua_close(state);
    free(budget.temporary);
    return status == LUA_OK;
}
