#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include "dali_driver.h"
#include "lua_dali.h"
#include "lua_system.h"

#define TAG "dali"

typedef struct
{
    int cbRef;
    int selfRef;
} dali_lua_callback_t;

static void free_cbctx(lua_State *L, dali_lua_callback_t *cb)
{
    luaL_unref(L, LUA_REGISTRYINDEX, cb->cbRef);
    luaL_unref(L, LUA_REGISTRYINDEX, cb->selfRef);
    free(cb);
}

static void command_callback(int result, void *arg)
{
    dali_lua_callback_t *cb = (dali_lua_callback_t *)arg;
    if (cb)
    {
        lua_State *L = acquireLuaMutex();
        if (cb->cbRef != LUA_REFNIL)
        {
            assert(lua_rawgeti(L, LUA_REGISTRYINDEX, cb->cbRef)); // The callback function
            if (!lua_isfunction(L, -1))
            {
                ESP_LOGE(TAG, "Callback value isn't a function");
                goto end;
            }
            // ESP_LOGI(TAG, "Using %d for self reference", cb->selfRef);
            if (cb->selfRef != LUA_REFNIL)
            {
                assert(lua_rawgeti(L, LUA_REGISTRYINDEX, cb->selfRef)); // Arg 1 - the self value for this callback
            }
            lua_pushinteger(L, result); // Arg 2 - the response
            // ESP_LOGW(TAG, "Calling Dali LUA callback for %d, %d", cb->cbRef, cb->selfRef);
            // dumpStack(L);
            if (lua_pcall(L, 2, 0, 0))
            {
                ESP_LOGE(TAG, "Error calling DALI callback: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
    end:
        free_cbctx(L, cb);
        releaseLuaMutex();
    }
}

/**
 * Sends the supplied command to the dali device specified in the first argument.
 */
static int transmit(lua_State *L)
{
    // ESP_LOGI(TAG, "Transmit");
    // dumpStack(L);
    // First arg is self
    if (!lua_istable(L, 1))
    {
        luaL_argerror(L, 1, "Self should be a driver object");
    }
    // Second arg is the command to transmit including address etc.  Must be a 16 bit integer.
    int cmd = luaL_checkinteger(L, 2);

    // Third arg is an optional function to call when we're done.
    dali_command_callback_t ccb = NULL;
    dali_lua_callback_t *cb = NULL;
    if (lua_isfunction(L, 3))
    {
        cb = (dali_lua_callback_t *)malloc(sizeof(dali_lua_callback_t));
        if (!cb)
        {
            luaL_error(L, "Could not allocate memory for callback context");
            return 1;
        }
        ccb = command_callback;
        lua_pushvalue(L, 3);
        cb->cbRef = luaL_ref(L, LUA_REGISTRYINDEX);

        // 4th argument is a value to use as 'self' for the callback call.  This may be optional or nil, but it will still be passed to the callback function as its first arg
        lua_pushvalue(L, 4);
        cb->selfRef = luaL_ref(L, LUA_REGISTRYINDEX);
        // ESP_LOGI(TAG, "Created refs %d %d", cb->cbRef, cb->selfRef);
        // dumpStack(L);
    }

    lua_getfield(L, 1, "driver");
    dali_driver_t *driver = (dali_driver_t *)lua_touserdata(L, -1);
    ccpeed_err_t err = dali_send_command(driver, cmd, ccb, cb);
    if (err != CCPEED_NO_ERR)
    {
        if (cb)
        {
            free_cbctx(L, cb);
        }
        luaL_error(L, "Could not transmit: %d", err);
        return 1;
    }
    return 0;
}

static int init_dali_driver(lua_State *L)
{
    // We expect three parameters - First is self, second is tx, third is rx.  self is used as metadata for returned object.

    int tx = luaL_checkinteger(L, 2);
    int rx = luaL_checkinteger(L, 3);

    lua_newtable(L);
    lua_pushstring(L, "driver");
    dali_driver_t *driver = (dali_driver_t *)lua_newuserdata(L, sizeof(dali_driver_t));
    if (!driver)
    {
        luaL_error(L, "Could not allocate memory for dali driver");
        return 1;
    }
    lua_settable(L, -3);

    ccpeed_err_t err = dali_driver_init(driver, tx, rx);
    if (err != CCPEED_NO_ERR)
    {
        luaL_error(L, "Could not create dali driver");
        return 1;
    }

    // Set the metadata table for our new object to be the class self, so that we can access the other functions as methods.
    // copy the value
    lua_pushvalue(L, 1);
    lua_setmetatable(L, -2);

    // Return the object
    return 1;
}

static const struct luaL_Reg dali_funcs[] = {
    {"new", init_dali_driver},
    {"transmit", transmit},
    {NULL, NULL}};

int luaopen_dali(lua_State *L)
{
    luaL_newlib(L, dali_funcs);

    // The class object is on the top of the stack.  We need to set its __index property to itself so that it can be used for method lookup
    // This is the equivalent of self.__index = self.
    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);

    return 1;
}