#ifndef AI_SANDBOX_H
#define AI_SANDBOX_H
#include <lauxlib.h>
#include <lua.h>

static int sandbox_repeat(lua_State *state) {
    size_t length, separator;
    luaL_checklstring(state, 1, &length);
    lua_Integer count = luaL_checkinteger(state, 2);
    luaL_optlstring(state, 3, "", &separator);
    if (count <= 0 || (!length && !separator)) {
        lua_pushliteral(state, "");
        return 1;
    }
    if (count > 65536)
        return luaL_error(state, "String repetition budget exceeded");
    int arguments = lua_gettop(state);
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, arguments, 1);
    return 1;
}

static void sandbox_limit_string(lua_State *state) {
    lua_getfield(state, -1, "rep");
    lua_pushcclosure(state, sandbox_repeat, 1);
    lua_setfield(state, -2, "rep");
}
#endif
