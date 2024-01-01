#include "cbor_helpers.h"
#include "lua_system.h"
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>

#define TAG "cbor_helpers"


CborError cbor_expect_uint16(CborValue *val, const uint16_t max_value, uint16_t *out) {
    uint32_t bigval;

    CborError err = cbor_expect_uint32(val, max_value, &bigval);
    if (err == CborNoError) {
        *out = bigval;
    }
    return err;
}

CborError cbor_expect_uint32(CborValue *val, const uint32_t max_value, uint32_t *out) {
    uint64_t tmp;

    if (!cbor_value_is_unsigned_integer(val)) {
        return CborErrorIllegalType;
    }

    CborError err = cbor_value_get_uint64(val, &tmp);
    if (err == CborNoError) {
        if (tmp <= max_value) {
            *out = (uint32_t) tmp;
        } else {
            return CborErrorIllegalNumber;
        }
    }
    return err;
}

CborError cbor_expect_bool(CborValue *val, bool *out) {
    bool tmp;

    if (!cbor_value_is_boolean(val)) {
        return CborErrorIllegalType;
    }

    CborError err = cbor_expect_bool(val, &tmp);
    if (err == CborNoError) {
        *out = tmp;
    }
    return err;
}


CborError lua_table_to_cbor(lua_State *L, CborEncoder *enc) {
    CborError err;
    CborEncoder objectEnc;

    err = cbor_encoder_create_map(enc, &objectEnc, CborIndefiniteLength);
    if (err != CborNoError) {
        return err;
    }

    lua_pushnil(L);
    while (lua_next(L, -2)) {
        // CBOR doesn't care what the key is, but be sensible.
        lua_pushvalue(L, -2); // copy the key to the top of the stack
        err = lua_to_cbor(L, &objectEnc);
        if (err != CborNoError) {
            lua_pop(L, 2);
            return err;
        }
        lua_pop(L, 1);
        // Encode the value. 
        err = lua_to_cbor(L, &objectEnc);
        if (err != CborNoError) {
            lua_pop(L, 2);
            return err;
        }
        lua_pop(L, 1); // Only pop the value.  Leave the key for the iterator.
    }
    err = cbor_encoder_close_container(enc, &objectEnc);
    return err;
}


CborError lua_array_to_cbor(lua_State *L, CborEncoder *enc) {
    CborError err;
    CborEncoder objectEnc;

    err = cbor_encoder_create_array(enc, &objectEnc, CborIndefiniteLength);
    int i = 1;
    do {
        lua_geti(L, -1, i++);
        if (!lua_isnil(L, -1)) {
            // Encode the value. 
            err = lua_to_cbor(L, &objectEnc);
            if (err != CborNoError) {
                lua_pop(L, 2);
                return err;
            }
        } else {
            // When we don't find a value, the list is done. 
            i = -1;
        }
        lua_pop(L, 1); // pop the value
    } while (i > 0);
    err = cbor_encoder_close_container(enc, &objectEnc);
    return err;
}


CborError lua_to_cbor(lua_State *L, CborEncoder *enc) {
    CborError err = CborErrorImproperValue;

    switch (lua_type(L, -1)) {
        case LUA_TBOOLEAN:
            err = cbor_encode_boolean(enc, lua_toboolean(L, -1));
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, -1)) {
                err = cbor_encode_int(enc, lua_tointeger(L, -1));
            } else {
                err = cbor_encode_double(enc, lua_tonumber(L, -1));
            }
            break;
        case LUA_TSTRING:
            size_t sz;
            const char *txt = lua_tolstring(L, -1, &sz);
            err = cbor_encode_text_string(enc, txt, sz);
            break;
        case LUA_TTABLE:
            // If the table has an item at key 1, treat it as an array
            lua_geti(L, -1, 1);
            bool asList= !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (asList) {
                // serialise as an array
                err = lua_array_to_cbor(L, enc);
            } else {
                // Serialise as an object.
                err = lua_table_to_cbor(L, enc);
            }
            break;
        
        // These are unsupported and will throw an error. 
        case LUA_TFUNCTION:
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
        case LUA_TTHREAD:
            ESP_LOGE(TAG, "Attempt to serialise non supported value - Will be replaced with null");
            // Fall through
        case LUA_TNIL:
            err = cbor_encode_null(enc);
            break;
    }
    return err;
}
