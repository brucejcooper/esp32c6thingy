#include "lua_cbor.h"
#include "lua_system.h"
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>

#define TAG "cbor"


typedef struct {
    const char *strval;
    int ival;
} str_type_pair_t;

static const str_type_pair_t hint_mappings[] = {
    { "int", CborIntegerType },
    { "double", CborDoubleType },
    { "float", CborFloatType },
    { "bstr", CborByteStringType },
    { "str", CborTextStringType },
    { "array", CborArrayType },
    { "map", CborMapType },
    { "bool", CborBooleanType },
    { "tag", CborTagType },
    { NULL, CborInvalidType }
};

static CborType to_typehint(lua_State *L, int pos) {
    const char *val = lua_tostring(L, pos);
    if (val) {
        for (str_type_pair_t *ptr = (str_type_pair_t *) hint_mappings; ptr->strval != NULL; ptr++) {
            if (strcmp(ptr->strval, val) == 0) {
                return ptr->ival;
            }
        }
    }
    return CborInvalidType;
}

static CborError encode_luaval(lua_State *L, CborEncoder *enc, CborType typehint, bool strict);



static CborError encode_lua_sequence(lua_State *L, CborEncoder *enc, bool strict) {
    CborError err;
    CborEncoder objectEnc;

    CborType typeHint = CborInvalidType;
    if (lua_getmetatable(L, -1)) {
        lua_getfield(L, -1, "__enc");
        typeHint = to_typehint(L, -1);
        lua_pop(L, 2);
    }

    err = cbor_encoder_create_array(enc, &objectEnc, CborIndefiniteLength);
    int i = 1;
    do {
        lua_geti(L, -1, i++);
        if (!lua_isnil(L, -1)) {
            // Encode the value. 
            err = encode_luaval(L, &objectEnc, typeHint, strict);
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



static CborError encode_lua_table(lua_State *L, CborEncoder *enc, CborType typehint, bool strict) {
    CborError err;
    CborEncoder objectEnc;


    // If the table has an item at key 1, treat it as an array
    CborType keyHint = CborInvalidType;
    CborType valHint = CborInvalidType;
    if (lua_getmetatable(L, -1)) {
        lua_getfield(L, -1, "__keyenc");
        keyHint = to_typehint(L, -1);
        lua_pop(L, 1); 

        lua_getfield(L, -1, "__valenc");
        valHint = to_typehint(L, -1);
        lua_pop(L, 1);

        // If it wasn't explicitly specified, see if there's an __enc for this to turn it into an array
        if (typehint == CborInvalidType) {
            lua_getfield(L, -1, "__enc");
            typehint = to_typehint(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    if (typehint == CborArrayType) {
        return encode_lua_sequence(L, enc, strict);
    }

    err = cbor_encoder_create_map(enc, &objectEnc, CborIndefiniteLength);
    if (err != CborNoError) {
        return err;
    }
    ESP_LOGD(TAG, "Encoding LUA table to CBOR using keyHint %d and valHint %d", keyHint, valHint);

    lua_pushnil(L);
    while (lua_next(L, -2)) {
        // CBOR doesn't care what the key is, but be sensible.
        lua_pushvalue(L, -2); // copy the key to the top of the stack
        err = encode_luaval(L, &objectEnc, keyHint, strict);
        if (err != CborNoError) {
            lua_pop(L, 2);
            return err;
        }
        lua_pop(L, 1);
        // Encode the value. 
        err = encode_luaval(L, &objectEnc, valHint, strict);
        if (err != CborNoError) {
            ESP_LOGE(TAG, "Error encoding value for key %s: %s", lua_tostring(L, -2), cbor_error_string(err));
            lua_pop(L, 2);
            return err;
        }
        lua_pop(L, 1); // Only pop the value.  Leave the key for the iterator.
        ESP_LOGV(TAG, "Next iter");
    }
    err = cbor_encoder_close_container(enc, &objectEnc);
    return err;
}


static CborError encode_luaval(lua_State *L, CborEncoder *enc, CborType typehint, bool strict) {
    CborError err = CborErrorImproperValue;

    switch (lua_type(L, -1)) {
        case LUA_TBOOLEAN:
            ESP_LOGD(TAG, "Encoding boolean");
            err = cbor_encode_boolean(enc, lua_toboolean(L, -1));
            break;
        case LUA_TNUMBER:
            if (typehint == CborInvalidType) {
                // Infer a type
                if (lua_isinteger(L, -1)) {
                    typehint = CborIntegerType;
                }
            }
            switch (typehint) {
                case CborIntegerType:
                    ESP_LOGD(TAG, "Encoding Integer");
                    err = cbor_encode_int(enc, lua_tointeger(L, -1));
                    break;
                case CborFloatType:
                    ESP_LOGD(TAG, "Encoding Float");
                    err = cbor_encode_float(enc, lua_tonumber(L, -1));
                    break;
                default:
                    ESP_LOGD(TAG, "Encoding Double");
                    err = cbor_encode_double(enc, lua_tonumber(L, -1));
                    break;
            }
            break;
        case LUA_TSTRING:
            size_t sz;
            const char *txt = lua_tolstring(L, -1, &sz);
            switch (typehint) {
                case CborByteStringType:
                    ESP_LOGD(TAG, "Encoding Byte String");
                    err = cbor_encode_byte_string(enc, (uint8_t *) txt, sz);
                    break;
                // We default to a text string rather than a byte string, because normally that will be what a user wants.
                default:
                    ESP_LOGD(TAG, "Encoding Text String");
                    err = cbor_encode_text_string(enc, txt, sz);
                    break;
            }
            break;

        case LUA_TTABLE:
            err = encode_lua_table(L, enc, typehint, strict);
            break;
        
        // These are unsupported and will throw an error. 
        case LUA_TFUNCTION:
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
        case LUA_TTHREAD:
            ESP_LOGE(TAG, "Attempt to serialise non supported value of type %s", lua_type_str(L, -1));
            return CborErrorIllegalType;
            // Fall through
        case LUA_TNIL:
            ESP_LOGD(TAG, "Encoding nil");
            err = cbor_encode_null(enc);
            break;
    }
    return err;
}


int lua_cbor_encode(lua_State *L) {
    CborType typeHint = CborInvalidType;
    uint8_t buf[1024];
    CborEncoder enc;

    int nArgs = lua_gettop(L);
    if (nArgs > 1) {
        // Second optional argument is the typehint for the top level object. Useful if its not a table with metadata (e.g. a number)
        typeHint = to_typehint(L, 2);
        lua_pop(L, nArgs-1); // We need the first arg to be the only thing on the stack
    }

    cbor_encoder_init(&enc, buf, sizeof(buf), 0);
    CborError err = encode_luaval(L, &enc, typeHint, false);
    if (err != CborNoError) {
        luaL_error(L, err == CborErrorIllegalType ? "Attempt to encode invalid value" :  cbor_error_string(err));
        return 1;
    }
    lua_pushlstring(L, (char *) buf, enc.data.ptr-buf);

    return 1;
}


/**
 * Decorator that takes a table and sets it metadata to one that indicates that the table should be encoded as a list.
 */
int lua_encode_as_list(lua_State *L) {
    if (!lua_istable(L, 1)) {
        luaL_argerror(L, 1, "Must be a table");
    }
    if (lua_getmetatable(L, 1)) {
        // It already has a metatable.  Just set the __enc field
        lua_pushstring(L, "array");
        lua_setfield(L, -2, "__enc");
        lua_pop(L, 1);
    } else {
        lua_getglobal(L, "cbor");
        lua_getfield(L, -1, "__list_meta");
        lua_setmetatable(L, 1);

    }
    lua_pop(L, 1); // the coap global

    lua_pushvalue(L, 1); // Returns what was passed in. 
    return 1;
}


static const struct luaL_Reg funcs[] = {
    { "encode", lua_cbor_encode},
    { "encode_as_list", lua_encode_as_list},
    { NULL, NULL }
};

int luaopen_cbor(lua_State *L)
{
    luaL_newlib(L, funcs);

    // Create some special values that cab be used as meta for special encodings.
    lua_pushstring(L, "__list_meta");
    lua_newtable(L);
    lua_pushstring(L, "__enc");
    lua_pushstring(L, "array");
    lua_settable(L, -3);
    lua_settable(L, -3);

    return 1;
}