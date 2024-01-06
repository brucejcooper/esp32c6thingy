#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include "lua_digest.h"
#include <esp_rom_md5.h>
#include <mbedtls/base64.h>

#define TAG "md5"


static int update(lua_State *L) {
    // First arg will be self.
    lua_getfield(L, 1, "digester");
    md5_context_t *ctx = (md5_context_t *) lua_touserdata(L, -1);

    size_t sz;
    uint8_t *data = (uint8_t*) lua_tolstring(L, 2, &sz);
    esp_rom_md5_update(ctx, data, sz);
    return 0;
}

static int digest(lua_State *L) {
    lua_getfield(L, 1, "digester");
    md5_context_t *ctx = (md5_context_t *) lua_touserdata(L, -1);
    if (lua_gettop(L) > 1) {
        update(L);
    }
    uint8_t digest[16];
    // uint8_t digestb64[32];
    // size_t olen;

    esp_rom_md5_final(digest, ctx);
    // assert(mbedtls_base64_encode(digestb64, sizeof(digestb64), &olen, digest, sizeof(digest)) == 0);

    lua_pushlstring(L, (char *) digest, 16);
    return 1;
}



static int new_md5(lua_State *L) {
    lua_newtable(L);
    
    lua_pushstring(L, "digester");
    md5_context_t *ctx = (md5_context_t *) lua_newuserdata(L, sizeof(md5_context_t));
    esp_rom_md5_init(ctx);
    lua_settable(L, -3);

    lua_pushstring(L, "update");
    lua_pushcfunction(L, update);
    lua_settable(L, -3);

    lua_pushstring(L, "digest");
    lua_pushcfunction(L, digest);
    lua_settable(L, -3);
    return 1;
}



static const struct luaL_Reg md5_funcs[] = {
    { "md5", new_md5 },
    { NULL, NULL }
};

int luaopen_digest(lua_State *L)
{
    luaL_newlib(L, md5_funcs);
    return 1;
}