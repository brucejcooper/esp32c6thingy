#ifndef MAIN_SYSTEM_H_
#define MAIN_SYSTEM_H_

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <esp_err.h>
#include "ccpeed_err.h"

#define LUA_ESP_ERR_CHECK(X) { int err = check_esp_err(L, X); if (err) return err; }

typedef struct {
    const char *sval;
    int ival;
} code_lookup_t;


typedef void (*lua_callback_helper_t)(lua_State *L, void *ctx);


void schedule_callback_from_ISR(lua_callback_helper_t fn, void *ctx);

void run_lua_loop();
void lua_report_error(lua_State *L, int status, const char *prefix);
void dumpStack(lua_State *L);
const char *lua_type_str(lua_State *L, int idx);


lua_State *acquireLuaMutex();
void releaseLuaMutex();
ccpeed_err_t lua_execute_callback(int fnRef, int nArgs);
int check_esp_err(lua_State *L, esp_err_t err);

int code_str_to_int(const char *str, const code_lookup_t *lookup);
const char *code_int_to_str(int code, const code_lookup_t *);
int lua_lookup(lua_State *L, int argIdx, const code_lookup_t *lookup);

#endif /* MAIN_SYSTEM_H_ */
