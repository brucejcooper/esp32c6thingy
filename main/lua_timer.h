#ifndef LUA_TIMER_H_
#define LUA_TIMER_H_

#include <lua/lua.h>
int lua_start_timer(lua_State *L);
int luaopen_timer(lua_State *L);

#endif /* LUA_TIMER_H_ */
