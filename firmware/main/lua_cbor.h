#pragma once

#include <stdint.h>
#include <cbor.h>
#include <lua/lua.h>


int lua_cbor_encode(lua_State *L);
int luaopen_cbor(lua_State *L);
