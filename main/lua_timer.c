#include <freertos/FreeRTOS.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <string.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include "lua_system.h"

#define TAG "timer"




typedef struct {
    esp_timer_handle_t timer;
    int timerRef;
} lua_timer_userdata_t;


/**
 * Ran on the main lua thread from a queue.
 */
static void timer_callback(lua_State *L, void *ctx) {
    lua_timer_userdata_t *ud = (lua_timer_userdata_t *) ctx;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ud->timerRef);
    lua_getfield(L, -1, "on_timeout");
    lua_pushvalue(L, -2); // Pass self as an argument
    if (lua_pcall(L, 1, 0, 0)) {
        ESP_LOGE(TAG, "Error running timer callback: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // Pop self.
}

static void timer_fired(void *arg) {
    schedule_callback_from_ISR(timer_callback, arg);
}


lua_timer_userdata_t *get_timer_userdata(lua_State *L, int argidx) {
    lua_getfield(L, 1, "_t");
    lua_timer_userdata_t *ud = (lua_timer_userdata_t *) lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ud;

}


int start_timer(lua_State *L) {
    int isnum;
    int delayMs = lua_tointegerx(L, 2, &isnum);
    bool repeat = false;

    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Expected table (self) in first arg");
        return 1;
    }

    if (!isnum || delayMs < 0) {
        luaL_argerror(L, 2, "Expected a positive number of milliseconds");
        return 1;
    }
    repeat = lua_toboolean(L, 3);

    lua_timer_userdata_t *ud = get_timer_userdata(L, 1);
    if (repeat) {
        LUA_ESP_ERR_CHECK(esp_timer_start_periodic(ud->timer, delayMs*1000));
    } else {
        LUA_ESP_ERR_CHECK(esp_timer_start_once(ud->timer, delayMs*1000));
    }
    return 0;
}

int restart_timer(lua_State *L) {
    int isnum;
    int delayMs = lua_tointegerx(L, 2, &isnum);
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Expected table (self) in first arg");
        return 1;
    }
    if (!isnum || delayMs < 0) {
        luaL_argerror(L, 2, "Expected a positive number of milliseconds");
        return 1;
    }
    lua_timer_userdata_t *ud = get_timer_userdata(L, 1);
    LUA_ESP_ERR_CHECK(esp_timer_restart(ud->timer, delayMs*1000));
    return 0;
}

int stop_timer(lua_State *L) {
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Expected table (self) in first arg");
        return 1;
    }
    lua_timer_userdata_t *ud = get_timer_userdata(L, 1);
    LUA_ESP_ERR_CHECK(esp_timer_stop(ud->timer));
    return 0;
}

int delete_timer(lua_State *L) {
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Expected table (self) in first arg");
        return 1;
    }
    lua_timer_userdata_t *ud = get_timer_userdata(L, 1);
    LUA_ESP_ERR_CHECK(esp_timer_delete(ud->timer));
    ud->timer = 0; // This should be an invalid value, so all subsequent calls will then fail. 
    return 0;
}



int lua_timer_new(lua_State *L) {
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Expected new to be called as a class method");
    }
    if (!lua_isfunction(L, 2)) {
        luaL_argerror(L, 2, "Expected function argument");
    }

    lua_newtable(L); // Our timer object
    lua_pushstring(L, "_t");
    lua_timer_userdata_t *ud = lua_newuserdata(L, sizeof(lua_timer_userdata_t));
    lua_settable(L, -3);

    // Create the timer
    esp_timer_create_args_t args = {
        .callback = timer_fired,
        .arg = ud,
        .name = NULL,
        .dispatch_method = ESP_TIMER_ISR
    };
    LUA_ESP_ERR_CHECK(esp_timer_create(&args, &ud->timer));

    lua_pushstring(L, "on_timeout");
    lua_pushvalue(L, 2);
    lua_settable(L, -3);

    // Make this object a class, using "self" (which should be Timer) as its meta-ref
    lua_pushvalue(L, 1);
    lua_setmetatable(L, -2);
    
    // Duplicate it so that we can take a ref
    lua_pushvalue(L, -1);
    ud->timerRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}

static const struct luaL_Reg funcs[] = {
    // { "start_task", start_task },
    { "new", lua_timer_new },
    { "start", start_timer },
    { "stop", stop_timer },
    { "restart", restart_timer },
    { "delete", delete_timer },
    { NULL, NULL }

};


int luaopen_timer(lua_State *L) {
    luaL_newlib(L, funcs);

    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    return 1;
}
