#ifndef MAIN_LUA_COAP_H_
#define MAIN_LUA_COAP_H_

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <openthread/udp.h>

int luaopen_coap(lua_State *L);
void coapCallback(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo);

#endif /* MAIN_LUA_COAP_H_ */
