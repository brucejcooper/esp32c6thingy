#ifndef MAIN_SYSTEM_H_
#define MAIN_SYSTEM_H_

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <esp_err.h>
#include "ccpeed_err.h"

typedef void (*lua_callback_helper_t)(lua_State *L, void *ctx);


void schedule_callback_from_ISR(lua_callback_helper_t fn, void *ctx);

void run_lua_loop();
void lua_report_error(lua_State *L, int status, const char *prefix);
void dumpStack(lua_State *L);
const char *lua_type_str(lua_State *L, int idx);


lua_State *acquireLuaMutex();
void releaseLuaMutex();
ccpeed_err_t lua_execute_callback(int fnRef, int nArgs);

#endif /* MAIN_SYSTEM_H_ */
