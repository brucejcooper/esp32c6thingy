#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>

static int register_device(lua_State *L)
{
    // We expect one parameter to be passed in - a Table. 
    int ms = luaL_checkinteger(L, 1);
    return 0;
}

static const struct luaL_Reg device_funcs[] = {
    { "register", register_device },
    { NULL, NULL }
};

int luaopen_device(lua_State *L)
{
    luaL_newlib(L, device_funcs);

    return 1;
}