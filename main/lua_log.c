#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include <string.h>
#include "lua_system.h"

#define TAG "logfactory"


static const code_lookup_t log_level_lookup[] = {
    {.sval = "none", .ival=ESP_LOG_NONE},
    {.sval = "error", .ival=ESP_LOG_ERROR},
    {.sval = "warn", .ival=ESP_LOG_WARN},
    {.sval = "info", .ival=ESP_LOG_INFO},
    {.sval = "debug", .ival=ESP_LOG_DEBUG},
    {.sval = "verbose", .ival=ESP_LOG_VERBOSE},
    {.sval = NULL, .ival=-1}
};

static inline int copy_string(char *buf, char *val, int remain) {
    int sz = strlen(val);
    if (remain > sz) {
        memcpy(buf, val, sz);
    }
    return sz;
}

static int do_log(lua_State *L, int log_level) {   
    // First arg is self (or at least it should be)
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Expected logger to be passed as first argument");
        return 1;
    }
    assert(lua_getfield(L, 1, "tag"));
    const char *tag = lua_tostring(L, -1);

    esp_log_level_t tag_level = esp_log_level_get(tag);
    if (log_level <= tag_level) {
        char buf[1024];
        char *bufptr = buf;
        int remain = sizeof(buf);


        int nargs = lua_gettop(L);
        for (int arg = 2; arg < nargs; arg++) {
            if (arg != 2) {
                *bufptr++ = '\t';
                remain--;
            }
            int sz;
            int ty = lua_type(L, arg);
            switch (ty) {
                case LUA_TSTRING:
                    char *strval = (char *) lua_tolstring(L, arg, (size_t *) &sz);
                    if (remain > sz) {
                        memcpy(bufptr, strval, sz);
                    }
                    break;
                case LUA_TBOOLEAN:
                    sz = copy_string(bufptr, lua_toboolean(L, arg) ? "true": "false", remain);
                    break;
                case LUA_TNUMBER:
                    if (lua_isinteger(L, arg)) {
                        sz = snprintf(bufptr, remain, "%lld", lua_tointeger(L, arg));
                    } else {
                        sz = snprintf(bufptr, remain, "%lf", lua_tonumber(L, arg));
                    }                
                    break;

                case LUA_TTABLE:
                    sz = snprintf(bufptr, remain, "table(%p)", lua_topointer(L, arg));

                    break;

                case LUA_TNIL:
                    sz = copy_string(bufptr, "NIL", remain);
                    break;

                default:
                    sz = snprintf(bufptr, remain, "UNK%d", ty);
                    break;
            }
            if (sz >= remain) {
                luaL_argerror(L, arg, "No more space in log buffer");
                return 1;
            }
            bufptr += sz;
            remain -= sz;
        }
        // Trailing 0
        *bufptr = 0;

        ESP_LOG_LEVEL(log_level, tag, "%s", buf);
    }
    return 0;

}

static int log_trace(lua_State *L) {
    return do_log(L, ESP_LOG_VERBOSE);
}

static int log_debug(lua_State *L) {
    return do_log(L, ESP_LOG_DEBUG);
}

static int log_info(lua_State *L) {
    return do_log(L, ESP_LOG_INFO);
}

static int log_warn(lua_State *L){
    return do_log(L, ESP_LOG_WARN);
}

static int log_error(lua_State *L){
    return do_log(L, ESP_LOG_ERROR);
}


static int log_get(lua_State *L){
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Logger:new not called as a class method/constructor");
    }
    if (!lua_isstring(L, 2)) {
        luaL_argerror(L, 2, "Expected name of logger");
    }

    assert(luaL_getsubtable(L, 1, "instances"));
    // If there isn't one, create it.
    if (!luaL_getsubtable(L, -1, lua_tostring(L, 2))) {
        lua_pop(L, 2);
        lua_newtable(L);
        lua_pushstring(L, "tag");
        lua_pushvalue(L, 2);
        lua_settable(L, -3);

        // Set the package's table as the metatable 
        lua_pushvalue(L, 1);
        lua_setmetatable(L, -2);

        // Append the instance onto the gloabl Logger.instances
        assert(luaL_getsubtable(L, 1, "instances"));
        lua_pushvalue(L, 2); // the tag
        lua_pushvalue(L, -3); // the instance
        lua_rawset(L, -3); // Do rawset to avoid metafunction
        lua_pop(L, 1);
    }
    
    return 1;
}

static int log_attrsetter(lua_State *L) {
    // Arg 1 is table
    // arg 2 is key
    // arg 3 is value
    if (strcmp(lua_tostring(L, 2), "level") == 0) {
        lua_getfield(L, 1, "tag");
        const char *tag = lua_tostring(L, -1);
        int newlvl = code_str_to_int(lua_tostring(L, 3), log_level_lookup);
        if (newlvl == -1) {
            luaL_argerror(L, 3, "Value must be a valid log level");
            return 1;
        }
        esp_log_level_set(tag, newlvl);
    } else {
        luaL_argerror(L, 2, "Can only set level on a logger");
        return 1;
    }
    return 0;
}


static int log_attrgetter(lua_State *L) {
    // Arg 1 is table
    // arg 2 is key
    const char *key = lua_tostring(L, 2);
    if (strcmp(key, "info") == 0) {
        lua_pushcfunction(L, log_info);
    } else if (strcmp(key, "debug") == 0) {
        lua_pushcfunction(L, log_debug);
    } else if (strcmp(key, "warn") == 0) {
        lua_pushcfunction(L, log_warn);
    } else if (strcmp(key, "error") == 0) {
        lua_pushcfunction(L, log_error);
    } else if (strcmp(key, "trace") == 0) {
        lua_pushcfunction(L, log_trace);
    } else if (strcmp(key, "level") == 0) {
        lua_getfield(L, 1, "tag");
        const char *tag = lua_tostring(L, -1);
        esp_log_level_t lvl = esp_log_level_get(tag);
        ESP_LOGI(TAG, "Log level for tag %s is %d", tag, lvl);
        lua_pushstring(L, code_int_to_str(lvl, log_level_lookup));
    } else {
        luaL_argerror(L, 2, "invalid key");
    }
    return 1;
}


static const struct luaL_Reg log_funcs[] = {
    { "get", log_get },
    { NULL, NULL }
};

int luaopen_logger(lua_State *L)
{
    luaL_newlib(L, log_funcs);


    // The metatable for the Logger singleton, to allow loggers to be created dynamically via Logger["foo"]
    // As logger will be used as a class metadata object, set its __index field. 
    lua_pushstring(L, "__index");
    lua_pushcfunction(L, log_attrgetter);
    lua_settable(L,-3);

    lua_pushstring(L, "__newindex");
    lua_pushcfunction(L, log_attrsetter);
    lua_settable(L,-3);

    // When loggers are created, we add them to this instance table. 
    lua_pushstring(L, "instances");
    lua_newtable(L);
    lua_settable(L,-3);


    return 1;
}