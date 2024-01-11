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

typedef struct {
    char *buf;
    char *ptr;
    size_t remain;
} str_appender_t;

static bool strbuf_appendstr(str_appender_t *strbuf, const char *msg) {
    size_t len = strlen(msg);
    if (len >= strbuf->remain) {
        len = strbuf->remain;
    }
    if (len > 0) {
        memcpy(strbuf->ptr, msg, len);
        strbuf->remain -= len;
        strbuf->ptr+= len;
        *strbuf->ptr = 0;
    }
    return strbuf->remain > 0;
}

static bool strbuf_appendnumber(str_appender_t *strbuf, lua_State *L, int arg) {
    size_t n;
    if (lua_isinteger(L, arg)) {
        n = snprintf(strbuf->ptr, strbuf->remain, "%lld", lua_tointeger(L, arg));
    } else {
        n = snprintf(strbuf->ptr, strbuf->remain, "%lf", lua_tonumber(L, arg));
    }       
    if (n < strbuf->remain) {
        strbuf->ptr+= n;
        strbuf->remain -= n;
        return true;
    } else {
        strbuf->ptr+= strbuf->remain;
        strbuf->remain = 0;
        return false;
    }
}

static bool strbuf_appendval(str_appender_t *strbuf, lua_State *L, int arg) {
    int valtype = lua_type(L,arg);
    if (valtype == LUA_TSTRING) {
        strbuf_appendstr(strbuf, "\"");
        strbuf_appendstr(strbuf, lua_tostring(L, arg));
        return strbuf_appendstr(strbuf, "\"");
    } else if (valtype == LUA_TNUMBER) {
        return strbuf_appendnumber(strbuf, L, arg);
    } else {
        return strbuf_appendstr(strbuf, lua_type_str(L, arg));
    }

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
        str_appender_t sbuf = {
            .buf = buf,
            .ptr = buf,
            .remain = sizeof(buf)-1
        };

        int nargs = lua_gettop(L);
        for (int arg = 2; arg < nargs; arg++) {
            if (arg != 2) {
                strbuf_appendstr(&sbuf, "\t");
            }
            int ty = lua_type(L, arg);
            switch (ty) {
                case LUA_TSTRING:
                    strbuf_appendstr(&sbuf, lua_tostring(L, arg));
                    break;
                case LUA_TBOOLEAN:
                    strbuf_appendstr(&sbuf, lua_toboolean(L, arg) ? "true": "false");
                    break;
                case LUA_TNUMBER:
                    strbuf_appendnumber(&sbuf, L, arg);
                    break;

                case LUA_TTABLE:
                    strbuf_appendstr(&sbuf, "{");    
                    int count = 0;
                    lua_pushnil(L);
                    while (lua_next(L, arg)) {
                        if (count++) {
                            strbuf_appendstr(&sbuf, ",");
                        }
                        strbuf_appendval(&sbuf, L, -2);
                        strbuf_appendstr(&sbuf, "=");
                        strbuf_appendval(&sbuf, L, -1);
                        lua_pop(L, 1);
                    }
                    strbuf_appendstr(&sbuf, "}");
                    break;

                case LUA_TNIL:
                    strbuf_appendstr(&sbuf, "nil");
                    break;

                default:
                    strbuf_appendstr(&sbuf, "unknown");
                    break;
            }
        }

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