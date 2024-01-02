#ifndef MAIN_SYSTEM_H_
#define MAIN_SYSTEM_H_

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <esp_err.h>

esp_err_t init_lua();
void lua_report_error(lua_State *L, int status, const char *prefix);
void dumpStack(lua_State *L);
int start_coro(lua_State *L);
const char *lua_type_str(lua_State *L, int idx);
void lua_lock_mine();
void lua_unlock_mine();

#endif /* MAIN_SYSTEM_H_ */
