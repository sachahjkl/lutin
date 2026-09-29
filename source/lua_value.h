#ifndef LUTIN_LUA_VALUE_H
#define LUTIN_LUA_VALUE_H
#include <cJSON.h>
#include <lauxlib.h>
#include <lua.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

enum {
    VALUE_MAX_DEPTH = 8,
    VALUE_MAX_NODES = 256,
    VALUE_MAX_STRING = 4096,
    VALUE_MAX_BYTES = 8192,
    VALUE_TOOL_MAX_DEPTH = 12,
    VALUE_TOOL_MAX_NODES = 1024,
    VALUE_TOOL_MAX_BYTES = 12288
};
typedef struct {
    unsigned nodes;
    size_t bytes;
    bool tool_result;
} ValueBudget;

static void lua_value_push(lua_State *state, const cJSON *value, unsigned depth,
                           unsigned max_depth) {
    if (depth > max_depth)
        luaL_error(state, "JSON nesting limit exceeded");
    luaL_checkstack(state, 3, "JSON stack unavailable");
    if (cJSON_IsObject(value) || cJSON_IsArray(value)) {
        lua_newtable(state);
        unsigned index = 1;
        const cJSON *child;
        cJSON_ArrayForEach(child, value) {
            lua_value_push(state, child, depth + 1, max_depth);
            if (cJSON_IsArray(value))
                lua_rawseti(state, -2, index++);
            else
                lua_setfield(state, -2, child->string);
        }
    } else if (cJSON_IsString(value))
        lua_pushstring(state, value->valuestring);
    else if (cJSON_IsNumber(value))
        lua_pushnumber(state, (lua_Number)value->valuedouble);
    else if (cJSON_IsBool(value))
        lua_pushboolean(state, cJSON_IsTrue(value));
    else
        lua_pushnil(state);
}

/* Bounded conversion rejects cycles through the depth limit. No metamethods
 * run. */
static cJSON *lua_value_json(lua_State *state, int index, unsigned depth,
                             ValueBudget *budget) {
    unsigned max_depth =
        budget->tool_result ? VALUE_TOOL_MAX_DEPTH : VALUE_MAX_DEPTH;
    unsigned max_nodes =
        budget->tool_result ? VALUE_TOOL_MAX_NODES : VALUE_MAX_NODES;
    size_t max_bytes =
        budget->tool_result ? VALUE_TOOL_MAX_BYTES : VALUE_MAX_BYTES;
    if (depth > max_depth || ++budget->nodes > max_nodes)
        return NULL;
    if (!lua_checkstack(state, 3))
        return NULL;
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        return cJSON_CreateNull();
    case LUA_TBOOLEAN:
        return cJSON_CreateBool(lua_toboolean(state, index));
    case LUA_TNUMBER: {
        double value = lua_tonumber(state, index);
        return isfinite(value) ? cJSON_CreateNumber(value) : NULL;
    }
    case LUA_TSTRING: {
        size_t length;
        const char *text = lua_tolstring(state, index, &length);
        budget->bytes += length;
        return budget->bytes <= max_bytes && length <= VALUE_MAX_STRING &&
                       strlen(text) == length
                   ? cJSON_CreateString(text)
                   : NULL;
    }
    case LUA_TTABLE: {
        size_t length = lua_rawlen(state, index), count = 0;
        bool array = length > 0;
        cJSON *result = array ? cJSON_CreateArray() : cJSON_CreateObject();
        if (!result)
            return NULL;
        lua_pushnil(state);
        while (lua_next(state, index)) {
            bool valid = array ? lua_isinteger(state, -2) &&
                                     lua_tointeger(state, -2) >= 1 &&
                                     (size_t)lua_tointeger(state, -2) <= length
                               : lua_type(state, -2) == LUA_TSTRING;
            if (!valid || ++count > max_nodes) {
                lua_pop(state, 2);
                cJSON_Delete(result);
                return NULL;
            }
            if (!array) {
                size_t key_length;
                const char *key = lua_tolstring(state, -2, &key_length);
                budget->bytes += key_length;
                cJSON *value =
                    budget->bytes <= max_bytes &&
                            key_length <= VALUE_MAX_STRING &&
                            strlen(key) == key_length
                        ? lua_value_json(state, -1, depth + 1, budget)
                        : NULL;
                if (!value || !cJSON_AddItemToObject(result, key, value)) {
                    cJSON_Delete(value);
                    lua_pop(state, 2);
                    cJSON_Delete(result);
                    return NULL;
                }
            }
            lua_pop(state, 1);
        }
        if (array) {
            if (count != length) {
                cJSON_Delete(result);
                return NULL;
            }
            for (size_t i = 1; i <= length; i++) {
                lua_rawgeti(state, index, (lua_Integer)i);
                cJSON *value = lua_value_json(state, -1, depth + 1, budget);
                lua_pop(state, 1);
                if (!value || !cJSON_AddItemToArray(result, value)) {
                    cJSON_Delete(value);
                    cJSON_Delete(result);
                    return NULL;
                }
            }
        }
        return result;
    }
    default:
        return NULL;
    }
}
#endif
