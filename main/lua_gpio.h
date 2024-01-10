#ifndef MAIN_GPIO_H_
#define MAIN_GPIO_H_

#include <lua/lua.h>
#include <lua/lauxlib.h>

int luaopen_gpio(lua_State *L);
void gpio_prepare_for_reset();
#endif /* MAIN_GPIO_H_ */
