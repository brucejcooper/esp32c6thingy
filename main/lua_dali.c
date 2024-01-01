#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include "dali_driver.h"
#include "lua_dali.h"

#define TAG "gpio"

/**
 * Sends the supplied command to the dali device specified in the first argument.
 */
static int cmd(lua_State *L, uint16_t cmd) {
    // First arg is self
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Self should be a driver object");
    }
    // Second arg is the address to hit. 
    int addr = luaL_checkinteger(L, 2);
    if (addr >= 0 && addr <= 63) {
        // Okay, we have a valid command.  Now we want to call yield with the right object.
        lua_newtable(L);
        lua_pushstring(L, "dali");
        lua_pushinteger(L, DALI_GEAR_ADDR(addr) | cmd);
        lua_settable(L, -3);

        // Copy the driver field out of the driver object into the arg table.
        lua_pushstring(L, "driver");
        lua_getfield(L, 1, "driver");
        lua_settable(L, -3);

        // Yield the result, which will instruct the event loop to run the command and return the result.
        return lua_yield(L, 1);
    } else {
        luaL_argerror(L, 1, "Address must be an integer between 0 and 63 inclusive");
        return 1;
    }

}


/**
 * Helper function that calculates the 16 bit command to send when querying the actual level of a fixture. 
 */
static int cmd_query_actual(lua_State *L) {
    return cmd(L, DALI_CMD_QUERY_ACTUAL_LEVEL);
}

/**
 * Helper function that calculates the 16 bit command to send when querying the actual level of a fixture. 
 */
static int cmd_goto_last_active(lua_State *L) {
    return cmd(L, DALI_CMD_GOTO_LAST_ACTIVE_LEVEL);
}

/**
 * Helper function that calculates the 16 bit command to send when querying the actual level of a fixture. 
 */
static int cmd_off(lua_State *L) {
    return cmd(L, DALI_CMD_OFF);
}



static int init_dali_driver(lua_State *L){
    // We expect three parameters - First is self, second is tx, third is rx.  self is used as metadata for returned object.

    int tx = luaL_checkinteger(L, 2);
    int rx = luaL_checkinteger(L, 3);


    dali_driver_t *driver = malloc(sizeof(dali_driver_t));
    if (!driver) {
        luaL_error(L, "Could not allocate memory for dali driver");
        return 1;
    }

    ccpeed_err_t err = dali_driver_init(driver, tx, rx);
    if (err != CCPEED_NO_ERR) {
        luaL_error(L, "Could not create dali driver");
        return 1;
    }


    // Create an object that represents the driver.
    lua_newtable(L);
    lua_pushstring(L, "driver");
    lua_pushlightuserdata(L, driver);
    lua_settable(L, -3);

    // Set the metadata table for our new object to be the class self, so that we can access the other functions as methods.
    // copy the value
    lua_pushvalue(L, 1);
    lua_setmetatable(L, -2);

    ESP_LOGI(TAG, "After init, stack at %d, top is %d", lua_gettop(L), lua_type(L, -1));

    // Return the object
    return 1;
}




static const struct luaL_Reg dali_funcs[] = {
    { "new", init_dali_driver },
    { "query_actual", cmd_query_actual },
    { "goto_last_active", cmd_goto_last_active }, 
    { "off", cmd_off },
    { NULL, NULL }
};

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