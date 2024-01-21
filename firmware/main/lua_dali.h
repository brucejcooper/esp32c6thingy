#ifndef MAIN_DALI_H_
#define MAIN_DALI_H_

#include <lua/lua.h>
#include <lua/lauxlib.h>

#define DALI_GEAR_ADDR(x) ((x) << 9)
#define DALI_GROUP_ADDR(x) (1 << 15 | (x) << 9)

#define DALI_ID_FROM_ADDR(x) ((x) >> 9)

int luaopen_dali(lua_State *L);

#endif /* MAIN_GPIO_H_ */
