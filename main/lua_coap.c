#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include <openthread/coap.h>
#include <openthread/udp.h>

#include <openthread/instance.h>
#include <esp_openthread.h>
#include <string.h>
#include "lua_system.h"
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include "ccpeed_err.h"
#include "lua_cbor.h"

#define TAG "coap"

typedef struct {
    otMessage *request_message;
    const otMessageInfo *aMessageInfo;
    otMessage *response;
} handler_ctx_t;





typedef struct {
    char *buf;
    ssize_t len; // -1 indicates nil or not present
} lstr_t;

typedef struct {
    size_t capacity;
    size_t len;
    lstr_t *items;
} lstr_arr_t;


typedef struct {
    uint8_t version; // Will always be 1
    lstr_t token;
    otCoapType type; 
    otCoapCode code;
    uint16_t message_id;
    lstr_arr_t uri_path;
    lstr_arr_t uri_query;
    lstr_arr_t if_match;
    bool if_none_match;
    lstr_t etag;

    otCoapOptionContentFormat contentFormat;

    int block1_id;
    otCoapBlockSzx block1_sz;
    bool block1_more;

    int block2_id;
    otCoapBlockSzx block2_sz;
    lstr_t payload;
} coap_packet_t;


// Based on COAP recommendations for maximum packet size.
#define COAP_PACKET_SZ (1024+128)

typedef struct {
    otMessageInfo msg_info;
    coap_packet_t req;

    // Packet forming stuff - Shouldn'e be touched by handlers. 
    otCoapOptionType last_opt;
    bool payload_started;
    uint8_t *ptr;

    uint8_t response_buf[COAP_PACKET_SZ]; 
    uint8_t request_buf[COAP_PACKET_SZ];
    uint8_t *response_payload_start;
    size_t payload_len;
} coap_response_t;


static int coapHandlerFuncitonRef = LUA_NOREF;



void lstr_arr_init(lstr_arr_t *arr, size_t init_cap) {
    arr->capacity = init_cap;
    arr->len = 0;
    if (init_cap == 0) {
        arr->items = NULL;
    } else {
        arr->items = malloc(sizeof(lstr_t) * init_cap);
        assert(arr->items);
    }
}

void lstr_arr_append(lstr_arr_t *arr, char *str, size_t len) {
    if (arr->capacity == arr->len) {
        arr->capacity = arr->capacity + 5;
        arr->items = realloc(arr->items, arr->capacity * sizeof(lstr_t));
        assert(arr->items);
    }
    lstr_t *i = arr->items + arr->len;
    i->buf = str;
    i->len = len;
    arr->len++;
}

void lstr_arr_free(lstr_arr_t *arr) {
    if (arr->items) {
        free(arr->items);
    }
    arr->items = NULL;
}

void coap_packet_init(coap_packet_t *pkt) {
    pkt->block2_id = -1,
    pkt->block2_sz = OT_COAP_OPTION_BLOCK_SZX_1024;
    pkt->block1_id = -1;
    pkt->block1_sz = OT_COAP_OPTION_BLOCK_SZX_1024;
    pkt->block1_more = false;
    pkt->etag.len = -1; // no e-tag
    lstr_arr_init(&pkt->uri_path, 3); // We give the path some initial capacity, because chances are it will be used.
    // The other string arrays are more unlikely to be used, so we start them out empty to save a few mallocs.
    lstr_arr_init(&pkt->if_match, 0);
    pkt->if_none_match = false;
    lstr_arr_init(&pkt->uri_query, 0);
    pkt->payload.len = -1;
    pkt->code = OT_COAP_CODE_EMPTY;
}

void coap_packet_free(coap_packet_t *pkt) {
    lstr_arr_free(&pkt->uri_path);
    lstr_arr_free(&pkt->uri_query);
    lstr_arr_free(&pkt->if_match);
}

uint32_t parseIntOpt(uint8_t *buf, size_t len) {
    uint32_t val = 0;
    while (len--) {
        val = val << 8 | *buf++;
    }
    return val;
}

bool parse_coap_packet(coap_packet_t *pkt, uint8_t *buf, size_t numRead) {
    uint8_t *ptr = buf;
    pkt->token.len = *ptr & 0x0F;
    *ptr >>= 4;
    pkt->type = *ptr & 0x03;
    *ptr >>= 2;
    pkt->version = *ptr++;
    pkt->code = *ptr++;
    pkt->message_id = ptr[0] << 8 | ptr[1];
    ptr += 2;
    pkt->token.buf = (char *) ptr;
    ptr += pkt->token.len;

    if (pkt->version != 1) {
        ESP_LOGE(TAG, "Incorrect version in COAP header - Ignoring packet");
        return false;
    }

    // Code must be one of the request codes
    if (pkt->code > OT_COAP_CODE_DELETE) {
        ESP_LOGW(TAG, "Incoming packet is not a request code.  Ignoring");
        return false;
    }

    ESP_LOGD(TAG, "Parsing CoAP request");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, numRead, ESP_LOG_VERBOSE);

    // Parse the options.
    otCoapOptionType opt = 0;
    ssize_t remaining = numRead+buf-ptr;
    while (remaining > 0 && *ptr != 0xFF) {
        ESP_LOGV(TAG, "Processing option starting with 0x%02x", *ptr);
        int delta = *ptr >> 4;
        int opt_len = *ptr & 0x0f;
        ptr++; 
        if (delta == 13) {
            delta += *ptr++;
        } else if (delta == 14) {
            delta = (*ptr++) << 8;
            delta += (*ptr++) + 269;
        } else if (delta == 15) {
            ESP_LOGW(TAG, "Message format error in option delta");
            return false;
        }

        if (opt_len == 13) {
            opt_len += *ptr++;
        } else if (opt_len == 14) {
            opt_len = (*ptr++) << 8;
            opt_len += (*ptr++) + 269;
        } else if (opt_len == 15) {
            ESP_LOGW(TAG, "Message format error in length");
            return false;
        }
        opt += delta;
        ESP_LOGV(TAG, "Opt Delta is %d, len = %d, opt = %d", delta, opt_len, opt);

        switch (opt) {
            case OT_COAP_OPTION_URI_PATH:
                lstr_arr_append(&pkt->uri_path, (char *) ptr, opt_len);
                break;
            case OT_COAP_OPTION_URI_QUERY:
                lstr_arr_append(&pkt->uri_query, (char *) ptr, opt_len);
                break;
            case OT_COAP_OPTION_BLOCK2:
                if (opt_len < 1 || opt_len > 3) {
                    ESP_LOGE(TAG, "Invalid Block2 length");
                    return false;
                }
                uint32_t block2Val = parseIntOpt(ptr, opt_len);
                pkt->block2_sz = block2Val & 0x07;
                pkt->block2_id = block2Val >> 4;
                ESP_LOGD(TAG, "REQ Block2 %d %d", pkt->block2_id, pkt->block2_sz);
                break;
            case OT_COAP_OPTION_IF_MATCH:
                lstr_arr_append(&pkt->if_match, (char *) ptr, opt_len);
                break;
            case OT_COAP_OPTION_IF_NONE_MATCH:
                // According to the RFC if-none-match has no value.  It only tests for existence
                pkt->if_none_match = false;
                break;
            case OT_COAP_OPTION_E_TAG:
                pkt->etag.buf = (char *) ptr;
                pkt->etag.len = opt_len;
                break;
            case OT_COAP_OPTION_BLOCK1:
                if (opt_len < 1 || opt_len > 3) {
                    ESP_LOGE(TAG, "Invalid Block1 length");
                    return false;
                }
                uint32_t block1Val = parseIntOpt(ptr, opt_len);
                pkt->block1_sz = block1Val & 0x07;
                pkt->block1_id = block1Val >> 4;
                pkt->block1_more = block1Val & 0x08 ? true : false;
                ESP_LOGD(TAG, "REQ Block1 %d %d %d", pkt->block1_id, pkt->block1_sz, pkt->block1_more);
                break;
            case OT_COAP_OPTION_URI_HOST:
            case OT_COAP_OPTION_OBSERVE:
            case OT_COAP_OPTION_URI_PORT:
            case OT_COAP_OPTION_LOCATION_PATH:
            case OT_COAP_OPTION_CONTENT_FORMAT:
            case OT_COAP_OPTION_MAX_AGE:
            case OT_COAP_OPTION_ACCEPT:
            case OT_COAP_OPTION_LOCATION_QUERY:
            case OT_COAP_OPTION_SIZE2:
            case OT_COAP_OPTION_PROXY_URI:
            case OT_COAP_OPTION_PROXY_SCHEME:
            case OT_COAP_OPTION_SIZE1:
            default:
                ESP_LOGW(TAG, "Ignoring Request Option 0x%02x len %d", opt, opt_len);
                break;
        }

        ptr += opt_len;
        remaining = numRead+buf-ptr;
    }
    ESP_LOGV(TAG, "After parsing options, there is %d remaining", remaining);
    if (remaining > 0) {
        if (*ptr != 0xFF) {
            ESP_LOGE(TAG, "Expected payload marker");
            return false;
        }
        ptr++; // Skip over the Payload marker byte
        remaining--;
        pkt->payload.buf = (char *) ptr;
        pkt->payload.len = remaining;
        return true;
    } else if (remaining < 0) {
        ESP_LOGE(TAG, "Parser error - overflow");
        return false;
    }
    // No payload
    pkt->payload.buf = NULL;
    pkt->payload.len = -1;
    return true;
}



static inline uint8_t coap_opt_encode_len(size_t len, uint16_t *ex, size_t *exlen) {
    if (len < 13) {
        *exlen = 0;
        return len;
    } 
    if (len < 269) {
        *exlen = 1;
        *ex = len - 13;
        return 13;
    }
    *exlen = 2;
    *ex = len - 269;
    return 14;
}

static inline size_t coap_response_space_remaining(coap_response_t *i) {
    size_t used = i->ptr - i->response_buf;
    return sizeof(i->response_buf) - used;
}


static inline bool coap_response_check_space(coap_response_t *i, size_t sz) {
    return coap_response_space_remaining(i) >= sz;
}

static inline bool coap_response_check_space_option(coap_response_t *i, size_t optSize){ 
    size_t len = 1 + optSize;
    if (optSize >= 269) {
        len += 2;
    } else if (optSize >= 13) {
        len++;
    }
    return coap_response_check_space(i, len);
}



static ccpeed_err_t coap_response_append_option(coap_response_t *i, const otCoapOptionType type, const uint8_t *val, size_t val_len) {
    if (!coap_response_check_space_option(i, val_len)) {
        return CCPEED_ERROR_NOMEM;
    }
    int bDiff = type - i->last_opt;

    if (bDiff < 0 || i->payload_started) {
        // options must be ever increasing. 
        return CCPEED_ERROR_INVALID;
    }
    uint16_t exDelta, exLen;
    size_t exDeltaSz, exValLenSz;
    *i->ptr++ = coap_opt_encode_len(bDiff, &exDelta, &exDeltaSz) << 4 | coap_opt_encode_len(val_len, &exLen, &exValLenSz);
    if (exDeltaSz == 2) {
        *i->ptr++ = exDelta >> 8;
    }
    if (exDeltaSz >=1) {
        *i->ptr++ = exDelta & 0xFF;
    }
    if (exValLenSz == 2) {
        *i->ptr++ = exLen >> 8;
    }
    if (exValLenSz >=1) {
        *i->ptr++ = exLen & 0xFF;
    }
    memcpy(i->ptr, val, val_len);
    i->ptr += val_len;
    
    return CCPEED_NO_ERR;
}

static ccpeed_err_t coap_response_append_uint_option(coap_response_t *i, const otCoapOptionType type, uint32_t val) {
    uint8_t buf[4];
    uint8_t *ptr = buf+3;
    int numBytes = 0;
    *ptr = 0;


    while (val) {
        *ptr-- = val & 0xFF;
        val >>= 8;
        numBytes++;
    }

    if (numBytes == 0) {
        // This isn't strictly necessary, but depending on other functions not doing stuff is bad practice.
        return coap_response_append_option(i, type, NULL, 0);
    }
    return coap_response_append_option(i, type, ptr+1, numBytes);
}


static inline void coap_response_append_content_marker(coap_response_t *i) {
    *i->ptr++ = 0xFF;
    i->response_payload_start = i->ptr;
    i->payload_started = true;
}



static ccpeed_err_t coap_response_append(coap_response_t *i, void *data, size_t len) {
    if (!coap_response_check_space(i, i->payload_started ? len : len+1)) {
        return CCPEED_ERROR_NOMEM;
    }
    if (!i->payload_started) {
        coap_response_append_content_marker(i);
    }
    // if we did in-place writing, we just want to move the pointer on
    memcpy(i->ptr, data, len);
    i->ptr += len;
    return CCPEED_NO_ERR;
}


static void coap_response_set_code(coap_response_t *i, otCoapCode code) {
    i->response_buf[1] = code;
}


void coap_response_reset_response(coap_response_t *i) {
    i->last_opt = 0;
    i->ptr = i->response_buf + 4 + i->req.token.len;
    i->payload_started = false;
}




bool coap_response_try_set_content_format(coap_response_t *i, otCoapOptionContentFormat fmt) {
    if (!i->payload_started) {
        return coap_response_append_uint_option(i, OT_COAP_OPTION_CONTENT_FORMAT, fmt) == CCPEED_NO_ERR;
    } else {
        return false;
    }

}


void lua_push_lstr_arr(lua_State *L, lstr_arr_t *a) {
    lua_newtable(L);

    int i = 1;
    int n = a->len;
    for (lstr_t *p = a->items; n--; p++) {
        lua_pushinteger(L, i++);
        lua_pushlstring(L, p->buf, p->len);
        lua_settable(L, -3);
    }
}


static const char *request_codes[] = {
    "empty",
    "get",
    "post",
    "put",
    "delete"
};

/**
 * We're about to call the lua COAP handler, and we need a lua friendly object to represent the interaction.
 */
void coap_prepare_handler_arg(lua_State *L, void *ctx) {
    coap_response_t *i = (coap_response_t *) ctx;

    lua_newtable(L);
    lua_pushstring(L, "code");
    assert(i->req.code <= OT_COAP_CODE_DELETE);
    lua_pushstring(L, request_codes[i->req.code]);
    lua_settable(L, -3);

    lua_pushstring(L, "message_id");
    lua_pushinteger(L, i->req.message_id);
    lua_settable(L, -3);

    lua_pushstring(L, "token");
    lua_pushlstring(L, i->req.token.buf, i->req.token.len);
    lua_settable(L, -3);

    lua_pushstring(L, "path");
    lua_push_lstr_arr(L, &i->req.uri_path);
    lua_settable(L, -3);

    if (i->req.uri_query.len > 0) {
        lua_pushstring(L, "query");
        lua_push_lstr_arr(L, &i->req.uri_query);
        lua_settable(L, -3);
    }

    if (i->req.if_match.len > 0) {
        lua_pushstring(L, "if_match");
        lua_push_lstr_arr(L, &i->req.if_match);
        lua_settable(L, -3);
    }

    if (i->req.if_none_match) {
        lua_pushstring(L, "if_none_match");
        lua_pushboolean(L, true);
        lua_settable(L, -3);
    }


    if (i->req.block2_id >= 0) {
        lua_pushstring(L, "block2");
        lua_newtable(L);
        lua_pushstring(L, "id");
        lua_pushinteger(L, i->req.block2_id);
        lua_settable(L, -3);

        lua_pushstring(L, "size");
        lua_pushinteger(L, 1 << (4 + i->req.block2_sz));
        lua_settable(L, -3);

        lua_settable(L, -3);
    }

    if (i->req.block1_id >= 0) {
        lua_pushstring(L, "block1");
        lua_newtable(L);
        lua_pushstring(L, "id");
        lua_pushinteger(L, i->req.block1_id);
        lua_settable(L, -3);

        lua_pushstring(L, "size");
        lua_pushinteger(L, 1 << (4 + i->req.block1_sz));
        lua_settable(L, -3);

        lua_pushstring(L, "more");
        lua_pushboolean(L, i->req.block1_more);
        lua_settable(L, -3);

        lua_settable(L, -3);
    }



    if (i->req.payload.len >= 0) {
        lua_pushstring(L, "payload");
        lua_pushlstring(L, i->req.payload.buf, i->req.payload.len);
        lua_settable(L, -3);
    }
}



static int new_response(lua_State *L) {
    // If they pass a table as the argument, use that as the response.  Otherwise, use the argument (which may be nil) as the payload
    ESP_LOGV(TAG, "Making new response Object");
    if (!lua_istable(L, 1)) {
        lua_newtable(L);
        lua_pushstring(L, "payload");
        lua_pushvalue(L, 1); // Put the argument as the body.
        lua_settable(L, -3);
    }

    // Set the coap global as the metatable.  This will be used as a marker to differentiate between a direct returned object and an explicit response
    lua_getglobal(L, "coap");
    lua_setmetatable(L, -2);

    // That's pretty much it.  We just want an object in which we can put values that will be passed back, and we need a way to differentiate it.
    return 1;
}


/**
 * Creates a response object with the supplied code
 */
static int new_code_response(lua_State *L, otCoapCode code) {
    // We create a new response object, using the argument as the constructor arguemnt;
    lua_pushcfunction(L, new_response);
    if (lua_gettop(L) == 1) {
        // There was no argument
        lua_pushnil(L);
    } else {
        lua_pushvalue(L, 1);
    }
    lua_call(L, 1, 1);
    lua_pushstring(L, "code");
    lua_pushinteger(L, code);
    lua_settable(L, -3);
    return 1;
}


int coap_block_opt(lua_State *L) {
    int id = lua_tointeger(L, 1);
    int size = lua_tointeger(L, 2);
    bool more = lua_toboolean(L, 3);
    int sizex;
    if (size == 16) {
        sizex = 0;
    } else if (size == 32) {
        sizex = 1;
    } else if (size == 64) {
        sizex = 2;
    } else if (size == 128) {
        sizex = 3;
    } else if (size == 256) {
        sizex = 4;
    } else if (size == 512) {
        sizex = 5;
    } else if (size == 1024) {
        sizex = 6;
    } else {
        luaL_argerror(L, 2, "Invalid size value");
        return 1;
    }
    uint32_t val = id << 4 | sizex;
    if (more) {
        val |= 0x08;
    }
    lua_pushinteger(L, val);
    return 1;
}

void log_response(coap_response_t *i) {
    // Response is at index 2
    // request is at index 3
    char path_buf[1024];
    char *ptr = path_buf;
    *ptr = 0;

    lstr_t *p = i->req.uri_path.items;
    for (int n = i->req.uri_path.len; n > 0; n--, p++) {
        *ptr++ = '/';
        memcpy(ptr, p->buf, p->len);
        ptr += p->len;
    }
    *ptr = 0;
    ESP_LOGI("request", "%s %s %d - %d.%d %d", request_codes[i->req.code], path_buf, i->req.payload.len, i->response_buf[1] >> 5, i->response_buf[1] & 0x1F, i->payload_started ? i->payload_len : -1);
}


/**
 * called when the lua task is finished. 
 * Stack args are 
 * 1. the status (boolean) - True if it worked, false if it errored.
 * 2. the result (any)
 * 3. the lua request object that we constructed
 */
void coap_handler_reply(lua_State *L, void *ctx) {
    coap_response_t *i = (coap_response_t *) ctx;
    ccpeed_err_t cerr;

    char *errmsg = NULL;
    size_t errmsglen = 0;

    ESP_LOGD(TAG, "Handler respone is %d %s %s", lua_toboolean(L, 1), lua_type_str(L, 2), lua_type_str(L, 2));
    
    // If arg 1 is false, then we encountered an unhandled error.
    if (!lua_toboolean(L, 1)) {
        errmsg = (char *) lua_tolstring(L, 2, &errmsglen);
        goto error;
    }

    bool isFullResponseObject = false;
    if (lua_istable(L, 2)) {
        // A table is a full response object if it's metadata table is the global coap object
        lua_getmetatable(L, 2);
        lua_getglobal(L, "coap");
        isFullResponseObject = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
    } else {
        ESP_LOGD(TAG, "arg is already a responeobject");
    }
    if (!isFullResponseObject) {
        ESP_LOGD(TAG, "Turning returned response into a full blown object");
        // encapsulate the original return in a new return object, with the payload set to the original return value.
        lua_newtable(L);
        lua_pushstring(L, "payload");
        lua_pushvalue(L, 2); // Put the return value in.
        lua_settable(L, -3);
        // Replace what was in arg 2 with the new object.
        lua_replace(L, 2);
    }

    // Set the code on the response, if present
    lua_getfield(L, 2, "code");
    if (lua_isinteger(L, -1)) {
        i->response_buf[1] = lua_tointeger(L, -1);
        ESP_LOGD(TAG, "Setting response code to 0x%02x", i->response_buf[1]);
    }
    lua_pop(L, 1);

    // Process options - remember they must be set in order
    // |      4 | ETag             | [RFC7252] |
    lua_getfield(L, 2, "etag");
    if (lua_isstring(L, -1)) {
        size_t sz;
        const uint8_t *etag = (const uint8_t *) lua_tolstring(L, -1, &sz);
        ESP_LOGD(TAG, "Setting e-tag to %.*s", sz, (char *) etag);
        coap_response_append_option(i, OT_COAP_OPTION_E_TAG, etag, sz);
    }
    lua_pop(L, 1);

    // |      8 | Location-Path    | [RFC7252] |

    // |     12 | Content-Format   | [RFC7252] |
    lua_getfield(L, 2, "format");
    if (lua_isstring(L, -1)) {
        const char *fmtS = lua_tostring(L, -1);
        ESP_LOGD(TAG, "Setting format to %s", fmtS);
        otCoapOptionContentFormat fmt = OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN;
        if (strcmp(fmtS, "cbor") == 0) {
            fmt = OT_COAP_OPTION_CONTENT_FORMAT_CBOR;
        }
        coap_response_append_uint_option(i, OT_COAP_OPTION_CONTENT_FORMAT, fmt);
    }
    lua_pop(L, 1);

    // |     14 | Max-Age          | [RFC7252] |
    // |     17 | Accept           | [RFC7252] |
    // |     20 | Location-Query   | [RFC7252] |

    // |    23 | Block2           | [RFC7959] |
    lua_getfield(L, 2, "block2");
    if (lua_isinteger(L, -1)) {
        ESP_LOGD(TAG, "encoding block2 response 0x%02llx", lua_tointeger(L, -1));
        if (coap_response_append_uint_option(i, OT_COAP_OPTION_BLOCK2, lua_tointeger(L, -1)) != CCPEED_NO_ERR) {
            errmsg = "Could not set block2 option";
            goto error;
        }
    }
    lua_pop(L, 1);
    
    // |     27 | Block1           | [RFC7959] |
    lua_getfield(L, 2, "block1");
    if (lua_isinteger(L, -1)) {
        ESP_LOGD(TAG, "Setting block1");
        if (coap_response_append_uint_option(i, OT_COAP_OPTION_BLOCK1, lua_tointeger(L, -1)) != CCPEED_NO_ERR) {
            errmsg = "could not set block1 option";
            goto error;
        }
    }
    lua_pop(L, 1);

    // |     28 | Size2            | [RFC7959] |
    // |     35 | Proxy-Uri        | [RFC7252] |
    // |     39 | Proxy-Scheme     | [RFC7252] |
    // |     60 | Size1            | [RFC7252] |

    // Options are done.  Now its a matter of putting out the payload
    lua_getfield(L, 2, "payload");
    ESP_LOGD(TAG, "Setting payload");
    switch (lua_type(L, -1)) {
        case LUA_TSTRING:
            // coap_response_try_set_content_format(i, OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN);
            size_t bodysz;
            uint8_t *body = (uint8_t *) lua_tolstring(L, -1, &bodysz);
            ESP_LOGD(TAG, "String body %d bytes", bodysz);
            cerr = coap_response_append(i, body, bodysz);
            if (cerr != CCPEED_NO_ERR) {
                errmsg = "No space in buffer";
                lua_pop(L, 1);
                goto error;
            }
            break;
        case LUA_TNIL:
            ESP_LOGD(TAG, "NIL body");
            // Nothing to append.
            break;
        default:
            errmsg = "Can not encode coroutine response type";
            lua_pop(L, 1);
            goto error;
    }
    lua_pop(L, 1);
    // If everything worked, jump over the error handling.
    goto reply;

error:
    // Rewrite the message as an error - this will overwrite any content already appended
    coap_response_reset_response(i);
    coap_response_set_code(i, OT_COAP_CODE_INTERNAL_ERROR);
    if (errmsglen == 0) {
        errmsglen = strlen(errmsg);
    }
    ESP_LOGE(TAG, "Error processing interaction: %.*s", errmsglen, errmsg);
    coap_response_append(i, errmsg, errmsglen);

reply:
    i->payload_len = i->payload_started ? i->ptr - i->response_payload_start : 0;
    // Now send the response.
    otInstance *instance = esp_openthread_get_instance();
    otMessage *respMsg = otUdpNewMessage(instance, NULL);
    int bufLen = i->ptr - i->response_buf;
    otMessageAppend(respMsg, i->response_buf, bufLen);
    ESP_LOGD(TAG, "Sending datagram response size %d", bufLen);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, i->response_buf, MIN(bufLen, 32), ESP_LOG_VERBOSE);

    log_response(i);
    otError oErr = otUdpSendDatagram(instance, respMsg, &(i->msg_info));
    if (oErr != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "could not send datagram");
        otMessageFree(respMsg);
    } else {
        char ipAddrBuf[OT_IP6_ADDRESS_STRING_SIZE];
        otIp6AddressToString(&i->msg_info.mPeerAddr, ipAddrBuf, OT_IP6_ADDRESS_STRING_SIZE);
    }
    coap_packet_free(&i->req);
    free(i);
}



void coapCallback(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo) {
    coap_response_t *interaction = malloc(sizeof(coap_response_t)); 
    if (!interaction) {
        ESP_LOGE(TAG, "Could not allocate message to process CoAP request.  Bailing out");
        return;
    }

    coap_packet_init(&interaction->req);
    memcpy(&interaction->msg_info, aMessageInfo, sizeof(otMessageInfo));

    size_t numRead = otMessageRead(aMessage, 0, interaction->request_buf, sizeof(interaction->request_buf));
    if (!parse_coap_packet(&interaction->req, interaction->request_buf, numRead)) {
        ESP_LOGW(TAG, "Could not parse COAP packet");
        coap_packet_free(&interaction->req);
        return;
    }

    interaction->response_buf[0] = interaction->req.token.len | OT_COAP_TYPE_ACKNOWLEDGMENT << 4 | 0x40;
    // A default (successful) code is set depending on the request method
    switch (interaction->req.code) {
        case OT_COAP_CODE_PUT:
        case OT_COAP_CODE_POST:
            interaction->response_buf[1] = OT_COAP_CODE_CHANGED;
            break;
        case OT_COAP_CODE_DELETE:
            interaction->response_buf[1] = OT_COAP_CODE_DELETED;
            break;
        default:
        case OT_COAP_CODE_GET:
            interaction->response_buf[1] = OT_COAP_CODE_CONTENT;
            break;
    }
    // Copy messageId and token into the response buffer.
    interaction->response_buf[2] = interaction->req.message_id >> 8;
    interaction->response_buf[3] = interaction->req.message_id & 0xFF;
    memcpy(interaction->response_buf+4, interaction->req.token.buf, interaction->req.token.len);
    coap_response_reset_response(interaction);


    // The respone buffer is now in a state where it can be processed and a response sent.  Schedule a lua coro to do the work.
    if (coapHandlerFuncitonRef > 0) {
        lua_create_task(coapHandlerFuncitonRef, interaction, coap_prepare_handler_arg, coap_handler_reply);
    } else {
        ESP_LOGW(TAG, "No CoAP Handler specified.  Packet will be ignored");
    }
}





static int set_coap_handler(lua_State *L) {
    if (!lua_isfunction(L, 1)) {
        luaL_argerror(L, 1, "Require a function argument to handle requests");
        return 1;
    }
    luaL_unref(L, LUA_REGISTRYINDEX, coapHandlerFuncitonRef); // De-reference the old one.
    coapHandlerFuncitonRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static int cbor_response(lua_State *L) {
    int nArgs = lua_gettop(L);
    lua_pushcfunction(L, new_response);
    lua_newtable(L);
    lua_pushstring(L, "cbor");
    lua_setfield(L, -2, "format");


    // Encode the payload
    lua_pushcfunction(L, lua_cbor_encode);
    // pass through args that we were called with to the encode function.
    for (int i = 1; i <= nArgs; i++) {
        lua_pushvalue(L, i);
    }
    lua_call(L, nArgs, 1);

    lua_setfield(L, -2, "payload");

    lua_call(L, 1, 1); // call new_response
    return 1;
}


#define ERR_FUNC(C) static int new_##C##_response(lua_State *L) { return new_code_response(L, OT_COAP_CODE_##C); }

ERR_FUNC(CREATED);
ERR_FUNC(DELETED);
ERR_FUNC(VALID);
ERR_FUNC(CHANGED);
ERR_FUNC(CONTENT);
ERR_FUNC(CONTINUE);

ERR_FUNC(BAD_REQUEST);
ERR_FUNC(UNAUTHORIZED);
ERR_FUNC(BAD_OPTION);
ERR_FUNC(FORBIDDEN);
ERR_FUNC(NOT_FOUND);
ERR_FUNC(METHOD_NOT_ALLOWED);
ERR_FUNC(NOT_ACCEPTABLE);
ERR_FUNC(REQUEST_INCOMPLETE);
ERR_FUNC(PRECONDITION_FAILED);
ERR_FUNC(REQUEST_TOO_LARGE);
ERR_FUNC(UNSUPPORTED_FORMAT);

ERR_FUNC(INTERNAL_ERROR);
ERR_FUNC(NOT_IMPLEMENTED);
ERR_FUNC(BAD_GATEWAY);
ERR_FUNC(SERVICE_UNAVAILABLE);
ERR_FUNC(GATEWAY_TIMEOUT);
ERR_FUNC(PROXY_NOT_SUPPORTED);


static const struct luaL_Reg coap_funcs[] = {
    // { "resource", registerResource },
    { "set_coap_handler", set_coap_handler },
    { "response", new_response },
    { "cbor_response", cbor_response },

    // Convenience functions for creating responses with a certain code. 
    { "created", new_CREATED_response },
    { "deleted", new_DELETED_response },
    { "valid", new_VALID_response },
    { "changed", new_CHANGED_response },
    { "content", new_CONTENT_response },
    { "continue", new_CONTINUE_response },

    { "bad_request", new_BAD_REQUEST_response },
    { "unauthorized", new_UNAUTHORIZED_response },
    { "bad_option", new_BAD_OPTION_response },
    { "forbidden", new_FORBIDDEN_response },
    { "not_found", new_NOT_FOUND_response },
    { "method_not_allowed", new_METHOD_NOT_ALLOWED_response },
    { "not_accpetable", new_NOT_ACCEPTABLE_response },
    { "request_incomplete", new_REQUEST_INCOMPLETE_response },
    { "precondition_failed", new_PRECONDITION_FAILED_response },
    { "request_too_large", new_REQUEST_TOO_LARGE_response },
    { "unsupported_format", new_UNSUPPORTED_FORMAT_response },

    { "internal_error", new_INTERNAL_ERROR_response },
    { "not_implemented", new_NOT_IMPLEMENTED_response },
    { "bad_gateway", new_BAD_GATEWAY_response },
    { "service_unavailable", new_SERVICE_UNAVAILABLE_response },
    { "gateway_timeout", new_GATEWAY_TIMEOUT_response },
    { "proxy_not_supported", new_PROXY_NOT_SUPPORTED_response },


    // Block option value
    { "block_opt", coap_block_opt},
    { NULL, NULL }
};

static void push_int_keypair(lua_State *L, const char *key, int val) {
    lua_pushstring(L, key); 
    lua_pushinteger(L, val); 
    lua_settable(L, -3);
}

int luaopen_coap(lua_State *L)
{
    luaL_newlib(L, coap_funcs);
    push_int_keypair(L, "CODE_EMPTY", OT_COAP_CODE_EMPTY);
    push_int_keypair(L, "CODE_GET", OT_COAP_CODE_GET);
    push_int_keypair(L, "CODE_POST", OT_COAP_CODE_POST);
    push_int_keypair(L, "CODE_PUT", OT_COAP_CODE_PUT);
    push_int_keypair(L, "CODE_DELETE", OT_COAP_CODE_DELETE);

    push_int_keypair(L, "CODE_RESPONSE_MIN", OT_COAP_CODE_RESPONSE_MIN);
    push_int_keypair(L, "CODE_CREATED", OT_COAP_CODE_CREATED);
    push_int_keypair(L, "CODE_DELETED", OT_COAP_CODE_DELETED);
    push_int_keypair(L, "CODE_VALID", OT_COAP_CODE_VALID);
    push_int_keypair(L, "CODE_CHANGED", OT_COAP_CODE_CHANGED);
    push_int_keypair(L, "CODE_CONTENT", OT_COAP_CODE_CONTENT);
    push_int_keypair(L, "CODE_CONTINUE", OT_COAP_CODE_CONTINUE);

    push_int_keypair(L, "CODE_BAD_REQUEST", OT_COAP_CODE_BAD_REQUEST);
    push_int_keypair(L, "CODE_UNAUTHORIZED", OT_COAP_CODE_UNAUTHORIZED);
    push_int_keypair(L, "CODE_BAD_OPTION", OT_COAP_CODE_BAD_OPTION);
    push_int_keypair(L, "CODE_FORBIDDEN", OT_COAP_CODE_FORBIDDEN);
    push_int_keypair(L, "CODE_NOT_FOUND", OT_COAP_CODE_NOT_FOUND);
    push_int_keypair(L, "CODE_METHOD_NOT_ALLOWED", OT_COAP_CODE_METHOD_NOT_ALLOWED);
    push_int_keypair(L, "CODE_NOT_ACCEPTABLE", OT_COAP_CODE_NOT_ACCEPTABLE);
    push_int_keypair(L, "CODE_REQUEST_INCOMPLETE", OT_COAP_CODE_REQUEST_INCOMPLETE);
    push_int_keypair(L, "CODE_PRECONDITION_FAILED", OT_COAP_CODE_PRECONDITION_FAILED);
    push_int_keypair(L, "CODE_REQUEST_TOO_LARGE", OT_COAP_CODE_REQUEST_TOO_LARGE);
    push_int_keypair(L, "CODE_UNSUPPORTED_FORMAT", OT_COAP_CODE_UNSUPPORTED_FORMAT);

    push_int_keypair(L, "CODE_INTERNAL_ERROR", OT_COAP_CODE_INTERNAL_ERROR);
    push_int_keypair(L, "CODE_NOT_IMPLEMENTED", OT_COAP_CODE_NOT_IMPLEMENTED);
    push_int_keypair(L, "CODE_BAD_GATEWAY", OT_COAP_CODE_BAD_GATEWAY);
    push_int_keypair(L, "CODE_SERVICE_UNAVAILABLE", OT_COAP_CODE_SERVICE_UNAVAILABLE);
    push_int_keypair(L, "CODE_GATEWAY_TIMEOUT", OT_COAP_CODE_GATEWAY_TIMEOUT);
    push_int_keypair(L, "CODE_PROXY_NOT_SUPPORTED", OT_COAP_CODE_PROXY_NOT_SUPPORTED);
    return 1;
}