#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

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

static const code_lookup_t hint_mappings[] = {
    { .sval="int", .ival=CborIntegerType },
    { .sval="double", .ival=CborDoubleType },
    { .sval="float", .ival=CborFloatType },
    { .sval="bstr", .ival=CborByteStringType },
    { .sval="str", .ival=CborTextStringType },
    { .sval="array", .ival=CborArrayType },
    { .sval="map", .ival=CborMapType },
    { .sval="bool", .ival=CborBooleanType },
    { .sval="tag", .ival=CborTagType },
    { .sval=NULL, .ival=CborInvalidType }
};

static CborType to_typehint(lua_State *L, int pos) {
    const char *s = lua_tostring(L, pos);
    if (s) {
        int ival = code_str_to_int(s, hint_mappings);
        if (ival >= 0) {
            return ival;
        }
    }
    return CborInvalidType;
}

static CborError encode_luaval(lua_State *L, int stackPos, CborEncoder *enc, uint8_t *buf, CborType typehint, bool strict);



static CborError encode_lua_sequence(lua_State *L, int stackPos, CborEncoder *enc, uint8_t *buf, bool strict) {
    CborError err;
    CborEncoder objectEnc;

    CborType typeHint = CborInvalidType;
    if (lua_getmetatable(L, stackPos)) {
        lua_getfield(L, stackPos, "__valenc");
        typeHint = to_typehint(L, -1);
        lua_pop(L, 2);
    }

    err = cbor_encoder_create_array(enc, &objectEnc, CborIndefiniteLength);
    int i = 1;
    do {
        lua_geti(L, stackPos, i++);
        if (!lua_isnil(L, -1)) {
            // Encode the value. 
            err = encode_luaval(L, -1, &objectEnc, buf, typeHint, strict);
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



static CborError encode_lua_table(lua_State *L, int stackPos, CborEncoder *enc, uint8_t *buf, CborType typehint, bool strict) {
    CborError err;
    CborEncoder objectEnc;

    int tableStackPos = lua_absindex(L, stackPos);
    int valueHintType = LUA_TNIL;

    if (lua_getmetatable(L, tableStackPos)) {
        lua_getfield(L, -1, "__keyenc");
        lua_getfield(L, -2, "__valenc");
        valueHintType = lua_type(L, -1);

        // If it wasn't explicitly specified, see if there's an __enc for this to turn it into an array
        if (typehint == CborInvalidType) {
            lua_getfield(L, -3, "__enc");
            typehint = to_typehint(L, -1);
            lua_pop(L, 1);
        }
        lua_remove(L, -3); // Remove the metadata table.
    } else {
        lua_pushnil(L); // No key hint
        lua_pushnil(L); // No value hint
        valueHintType = LUA_TNIL;
    }


    if (typehint == CborArrayType) {
        lua_pop(L, 2); // We don't need the hints.
        return encode_lua_sequence(L, tableStackPos, enc, buf, strict);
    }

    err = cbor_encoder_create_map(enc, &objectEnc, CborIndefiniteLength);
    if (err != CborNoError) {
        return err;
    }

    lua_pushnil(L);
    ESP_LOGD(TAG, "Dumping a table, value pos is %d", tableStackPos);
    while (lua_next(L, tableStackPos)) {
        // CBOR doesn't care what the key is, but be sensible.  Be careful not to call lua_tostring() because this messes up the key if it isn't already a string

        CborType keyHint = to_typehint(L, -4);
        err = encode_luaval(L, -2, &objectEnc, buf, keyHint, strict);
        if (err != CborNoError) {
            ESP_LOGE(TAG, "Error encoding key: %s", cbor_error_string(err));
            lua_pop(L, 4);
            return err;
        }
        ESP_LOGD(TAG, "Key hint is %d", keyHint);

        // Encode the value.  Hint will depend on value from metatable. 
        CborType valHint;
        if (valueHintType == LUA_TTABLE) {
            // Look up the key in the hint table. 
            lua_getfield(L, -3, lua_tostring(L, -2));
            valHint = to_typehint(L, -1);
            lua_pop(L, 1);
        } else {
            valHint = to_typehint(L, -3);
        }
        ESP_LOGD(TAG, "Value hint is %d", keyHint);

        err = encode_luaval(L, -1,  &objectEnc, buf, valHint, strict);
        if (err != CborNoError) {
            ESP_LOGE(TAG, "Error encoding value for key: %s", cbor_error_string(err));
            lua_pop(L, 4);
            return err;
        }
        lua_pop(L, 1); // Only pop the value.  Leave the key for the iterator.

    }
    err = cbor_encoder_close_container(enc, &objectEnc);

    lua_pop(L, 2); // the two hints.
    return err;
}


static CborError encode_luaval(lua_State *L, int stackPos, CborEncoder *enc,  uint8_t *buf, CborType typehint, bool strict) {
    CborError err = CborErrorImproperValue;

    switch (lua_type(L, stackPos)) {
        case LUA_TBOOLEAN:
            ESP_LOGD(TAG, "Encoding boolean");
            err = cbor_encode_boolean(enc, lua_toboolean(L, stackPos));
            break;
        case LUA_TNUMBER:
            if (typehint == CborInvalidType) {
                // Infer a type
                if (lua_isinteger(L, stackPos)) {
                    typehint = CborIntegerType;
                }
            }
            switch (typehint) {
                case CborIntegerType:
                    ESP_LOGD(TAG, "Encoding Integer");
                    err = cbor_encode_int(enc, lua_tointeger(L, stackPos));
                    break;
                case CborFloatType:
                    ESP_LOGD(TAG, "Encoding Float");
                    err = cbor_encode_float(enc, lua_tonumber(L, stackPos));
                    break;
                default:
                    ESP_LOGD(TAG, "Encoding Double");
                    err = cbor_encode_double(enc, lua_tonumber(L, stackPos));
                    break;
            }
            break;
        case LUA_TSTRING:
            size_t sz;
            const char *txt = lua_tolstring(L, stackPos, &sz);
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
            err = encode_lua_table(L, stackPos, enc, buf, typehint, strict);
            break;
        
        // These are unsupported and will throw an error. 
        case LUA_TFUNCTION:
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
        case LUA_TTHREAD:
            ESP_LOGE(TAG, "Attempt to serialise non supported value of type %s", lua_type_str(L, stackPos));
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
    CborError err = encode_luaval(L, -1, &enc, buf, typeHint, false);
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