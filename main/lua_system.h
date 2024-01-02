#ifndef MAIN_SYSTEM_H_
#define MAIN_SYSTEM_H_

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <esp_err.h>
#include "ccpeed_err.h"

typedef void (*coro_helper_fn_t)(lua_State *L, void *ctx);


esp_err_t init_lua();
void lua_report_error(lua_State *L, int status, const char *prefix);
void dumpStack(lua_State *L);
const char *lua_type_str(lua_State *L, int idx);

ccpeed_err_t lua_create_task(int fnRef, void *ctx, coro_helper_fn_t argHelper, coro_helper_fn_t postprocessor);


#endif /* MAIN_SYSTEM_H_ */
