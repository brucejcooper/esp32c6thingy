#pragma once

#include <stdint.h>
#include <cbor.h>
#include <lua/lua.h>

CborError cbor_expect_uint32(CborValue *val, const uint32_t max_value, uint32_t *out);
CborError cbor_expect_uint16(CborValue *val, const uint16_t max_value, uint16_t *out);
CborError cbor_expect_bool(CborValue *val, bool *out);


CborError lua_to_cbor(lua_State *L, CborEncoder *enc);