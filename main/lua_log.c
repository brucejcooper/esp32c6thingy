#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "lua";


static inline int copy_string(char *buf, char *val, int remain) {
    int sz = strlen(val);
    if (remain > sz) {
        memcpy(buf, val, sz);
    }
    return sz;

}

static int do_log(lua_State *L, int log_level) {   
    if ( LOG_LOCAL_LEVEL >= log_level ) {
        char buf[1024];
        char *bufptr = buf;
        int remain = sizeof(buf);


        int nargs = lua_gettop(L);
        for (int arg = 1; arg <= nargs; arg++) {
            if (arg != 1) {
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
                    sz = copy_string(bufptr, "table", remain);
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

        ESP_LOG_LEVEL_LOCAL(log_level, TAG, "%s", buf);
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

static const struct luaL_Reg log_funcs[] = {
    { "trace", log_trace },
    { "debug", log_debug },
    { "info", log_info },
    { "warn", log_warn },
    { "error", log_error },
    { NULL, NULL }
};

int luaopen_log(lua_State *L)
{
    luaL_newlib(L, log_funcs);

    return 1;
}