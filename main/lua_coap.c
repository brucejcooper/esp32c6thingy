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


// Based on COAP recommendations for maximum packet size.
#define COAP_PACKET_SZ (1024+128)


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
    uint8_t buf[COAP_PACKET_SZ];
} coap_packet_t;



typedef struct {
    // otMessageInfo msg_info;
    // Packet forming stuff - Shouldn'e be touched by handlers. 
    otCoapOptionType last_opt;
    bool payload_started;
    uint8_t *ptr;

    uint8_t response_buf[COAP_PACKET_SZ]; 
    uint8_t *response_payload_start;
    size_t payload_len;
} coap_response_t;



static const code_lookup_t code_lookup[] = {
    {.sval="empty", .ival=OT_COAP_CODE_EMPTY},
    { .sval="get", .ival=OT_COAP_CODE_GET},
    { .sval="post", .ival=OT_COAP_CODE_POST},
    { .sval="put", .ival=OT_COAP_CODE_PUT},
    { .sval="delete", .ival=OT_COAP_CODE_DELETE},

    { .sval="success", .ival=OT_COAP_CODE_RESPONSE_MIN},
    { .sval="created", .ival=OT_COAP_CODE_CREATED},
    { .sval="deleted", .ival=OT_COAP_CODE_DELETED},
    { .sval="valid", .ival=OT_COAP_CODE_VALID},
    { .sval="changed", .ival=OT_COAP_CODE_CHANGED},
    { .sval="content", .ival=OT_COAP_CODE_CONTENT},
    { .sval="continue", .ival=OT_COAP_CODE_CONTINUE},

    { .sval="bad_request", .ival=OT_COAP_CODE_BAD_REQUEST},
    { .sval="unauthorized", .ival=OT_COAP_CODE_UNAUTHORIZED},
    { .sval="bad_option", .ival=OT_COAP_CODE_BAD_OPTION},
    { .sval="forbidden", .ival=OT_COAP_CODE_FORBIDDEN},
    { .sval="not_found", .ival=OT_COAP_CODE_NOT_FOUND},
    { .sval="method_not_allowed", .ival=OT_COAP_CODE_METHOD_NOT_ALLOWED},
    { .sval="not_acceptable", .ival=OT_COAP_CODE_NOT_ACCEPTABLE},
    { .sval="request_incomplete", .ival=OT_COAP_CODE_REQUEST_INCOMPLETE},
    { .sval="precondition_failed", .ival=OT_COAP_CODE_PRECONDITION_FAILED},
    { .sval="request_too_large", .ival=OT_COAP_CODE_REQUEST_TOO_LARGE},
    { .sval="unsupported_format", .ival=OT_COAP_CODE_UNSUPPORTED_FORMAT},

    { .sval="internal_error", .ival=OT_COAP_CODE_INTERNAL_ERROR},
    { .sval="not_implemented", .ival=OT_COAP_CODE_NOT_IMPLEMENTED},
    { .sval="bad_gateway", .ival=OT_COAP_CODE_BAD_GATEWAY},
    { .sval="service_unavailable", .ival=OT_COAP_CODE_SERVICE_UNAVAILABLE},
    { .sval="gateway_timeout", .ival=OT_COAP_CODE_GATEWAY_TIMEOUT},
    { .sval="proxy_not_supported", .ival=OT_COAP_CODE_PROXY_NOT_SUPPORTED},
    {.sval=NULL, .ival=0}
};





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

bool parse_coap_packet(coap_packet_t *pkt, const uint8_t *buf, size_t numRead) {
    uint8_t *ptr = (uint8_t *) buf;
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



otCoapCode getRequestCode(lua_State *L, int argIdx) {
    lua_getfield(L, argIdx, "code");
    const char *codestr = lua_tostring(L, -1);
    otCoapCode req_code;
    switch (codestr[0]) {
        case 'g': req_code = OT_COAP_CODE_GET; break;
        case 'p': req_code = codestr[1] == 'u' ? OT_COAP_CODE_PUT: OT_COAP_CODE_POST; break;
        case 'd': req_code = OT_COAP_CODE_DELETE; break;
        default:
            return OT_COAP_CODE_INTERNAL_ERROR;
    }
    lua_pop(L, 1);
    return req_code;
}



/**
 * Serialises a table representing a COAP message to a CoAP binary packet.
 */
static int lua_coap_serialise(lua_State *L) {
    coap_response_t resp;

    size_t tokensz;
    lua_getfield(L, 1, "token");
    const char *token = lua_tolstring(L, -1, &tokensz);
    memcpy(resp.response_buf+4, token, tokensz);
    lua_pop(L, 1);

    resp.response_buf[0] = tokensz | OT_COAP_TYPE_ACKNOWLEDGMENT << 4 | 0x40;
    // A default (successful) code is set depending on the request method


    // Copy messageId and token into the response buffer.
    lua_getfield(L, 1, "message_id");
    uint16_t msgid = lua_tointeger(L, -1);
    lua_pop(L, 1);


    resp.response_buf[2] = msgid >> 8;
    resp.response_buf[3] = msgid & 0xFF;

    resp.last_opt = 0;
    resp.ptr = resp.response_buf + 4 + tokensz;
    resp.payload_started = false;

    otCoapCode req_code =  getRequestCode(L, 1);

    // Set the code on the response, if present
    lua_getfield(L, 1, "code");
    if (lua_isstring(L, -1)) {
        int ival = code_str_to_int(lua_tostring(L, -1), code_lookup);
        if (ival == -1) {
            luaL_argerror(L, 1, "response code is invlalid");
        }
        resp.response_buf[1] = ival;
        ESP_LOGD(TAG, "Setting response code to 0x%02x", resp.response_buf[1]);
    } else {
        ESP_LOGD(TAG, "Inferring response code based on request code.");
        switch (req_code) {
            case OT_COAP_CODE_PUT:
            case OT_COAP_CODE_POST:
                resp.response_buf[1] = OT_COAP_CODE_CHANGED;
                break;
            case OT_COAP_CODE_DELETE:
                resp.response_buf[1] = OT_COAP_CODE_DELETED;
                break;
            case OT_COAP_CODE_GET:
                resp.response_buf[1] = OT_COAP_CODE_CONTENT;
                break;
            default:
                luaL_error(L, "Request has illegal code");
                return 1;

        }
    }
    lua_pop(L, 1);

    // Process options - remember they must be set in order
    // |      4 | ETag             | [RFC7252] |
    lua_getfield(L, 1, "etag");
    if (lua_isstring(L, -1)) {
        size_t sz;
        const uint8_t *etag = (const uint8_t *) lua_tolstring(L, -1, &sz);
        ESP_LOGD(TAG, "Setting e-tag to %.*s", sz, (char *) etag);
        coap_response_append_option(&resp, OT_COAP_OPTION_E_TAG, etag, sz);
    }
    lua_pop(L, 1);

    // |      8 | Location-Path    | [RFC7252] |

    // |     12 | Content-Format   | [RFC7252] |
    lua_getfield(L, 1, "format");
    if (lua_isstring(L, -1)) {
        const char *fmtS = lua_tostring(L, -1);
        ESP_LOGD(TAG, "Setting format to %s", fmtS);
        otCoapOptionContentFormat fmt = OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN;
        if (strcmp(fmtS, "cbor") == 0) {
            fmt = OT_COAP_OPTION_CONTENT_FORMAT_CBOR;
        }
        coap_response_append_uint_option(&resp, OT_COAP_OPTION_CONTENT_FORMAT, fmt);
    }
    lua_pop(L, 1);

    // |     14 | Max-Age          | [RFC7252] |
    // |     17 | Accept           | [RFC7252] |
    // |     20 | Location-Query   | [RFC7252] |

    // |    23 | Block2           | [RFC7959] |
    lua_getfield(L, 1, "block2");
    if (lua_isinteger(L, -1)) {
        ESP_LOGD(TAG, "encoding block2 response 0x%02llx", lua_tointeger(L, -1));
        if (coap_response_append_uint_option(&resp, OT_COAP_OPTION_BLOCK2, lua_tointeger(L, -1)) != CCPEED_NO_ERR) {
            luaL_argerror(L, 1, "Could not set block2 option");
            return 1;
        }
    }
    lua_pop(L, 1);
    
    // |     27 | Block1           | [RFC7959] |
    lua_getfield(L, 1, "block1");
    if (lua_isinteger(L, -1)) {
        ESP_LOGD(TAG, "Setting block1");
        if (coap_response_append_uint_option(&resp, OT_COAP_OPTION_BLOCK1, lua_tointeger(L, -1)) != CCPEED_NO_ERR) {
            luaL_argerror(L, 1, "Could not set block1 option");
            return 1;
        }
    }
    lua_pop(L, 1);

    // |     28 | Size2            | [RFC7959] |
    // |     35 | Proxy-Uri        | [RFC7252] |
    // |     39 | Proxy-Scheme     | [RFC7252] |
    // |     60 | Size1            | [RFC7252] |

    // Options are done.  Now its a matter of putting out the payload
    lua_getfield(L, 1, "payload");
    ESP_LOGD(TAG, "Setting payload");
    switch (lua_type(L, -1)) {
        case LUA_TSTRING:
            // coap_response_try_set_content_format(i, OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN);
            size_t bodysz;
            uint8_t *body = (uint8_t *) lua_tolstring(L, -1, &bodysz);
            ESP_LOGV(TAG, "String body %d bytes: %s", bodysz, body);

            if (!coap_response_check_space(&resp, bodysz+1)) {
                luaL_argerror(L, 1, "payload too large for packet");
                return 1;
            }

            // Append the content marker
            *resp.ptr++ = 0xFF;
            resp.response_payload_start = resp.ptr;
            resp.payload_started = true;


            // if we did in-place writing, we just want to move the pointer on
            memcpy(resp.ptr, body, bodysz);
            resp.ptr += bodysz;
            break;
        case LUA_TNIL:
            ESP_LOGD(TAG, "NIL body");
            // Nothing to append.
            break;
        default:
            luaL_argerror(L, 1, "Payload must be a string or nil");
            return 1;
    }
    lua_pop(L, 1);

    size_t sz = resp.ptr-resp.response_buf;
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, resp.response_buf, sz, ESP_LOG_VERBOSE);
    lua_pushlstring(L, (char *) resp.response_buf, sz);
    return 1;
}


/**
 * Arg 1 is a string
*/
static int lua_coap_parse(lua_State *L) {
    coap_packet_t req;
    coap_packet_init(&req);

    size_t sz; 
    const uint8_t *buf = (const uint8_t *) lua_tolstring(L, 1, &sz);
    if (parse_coap_packet(&req, buf, sz)) {
        lua_newtable(L);
        // lua_pushstring(L, "msginfo");
        // otMessageInfo *msgInfoCopy = lua_newuserdata(L, sizeof(otMessageInfo));
        // memcpy(msgInfoCopy, msginfo, sizeof(otMessageInfo));
        // lua_settable(L, -3);

        lua_pushstring(L, "code");
        lua_pushstring(L, code_int_to_str(req.code, code_lookup));
        lua_settable(L, -3);

        lua_pushstring(L, "message_id");
        lua_pushinteger(L, req.message_id);
        lua_settable(L, -3);

        lua_pushstring(L, "token");
        lua_pushlstring(L, req.token.buf, req.token.len);
        lua_settable(L, -3);

        lua_pushstring(L, "path");
        lua_push_lstr_arr(L, &req.uri_path);
        lua_settable(L, -3);

        if (req.uri_query.len > 0) {
            lua_pushstring(L, "query");
            lua_push_lstr_arr(L, &req.uri_query);
            lua_settable(L, -3);
        }

        if (req.if_match.len > 0) {
            lua_pushstring(L, "if_match");
            lua_push_lstr_arr(L, &req.if_match);
            lua_settable(L, -3);
        }

        if (req.if_none_match) {
            lua_pushstring(L, "if_none_match");
            lua_pushboolean(L, true);
            lua_settable(L, -3);
        }


        if (req.block2_id >= 0) {
            lua_pushstring(L, "block2");
            lua_newtable(L);
            lua_pushstring(L, "id");
            lua_pushinteger(L, req.block2_id);
            lua_settable(L, -3);

            lua_pushstring(L, "size");
            lua_pushinteger(L, 1 << (4 + req.block2_sz));
            lua_settable(L, -3);

            lua_settable(L, -3);
        }

        if (req.block1_id >= 0) {
            lua_pushstring(L, "block1");
            lua_newtable(L);
            lua_pushstring(L, "id");
            lua_pushinteger(L, req.block1_id);
            lua_settable(L, -3);

            lua_pushstring(L, "size");
            lua_pushinteger(L, 1 << (4 + req.block1_sz));
            lua_settable(L, -3);

            lua_pushstring(L, "more");
            lua_pushboolean(L, req.block1_more);
            lua_settable(L, -3);

            lua_settable(L, -3);
        }

        if (req.payload.len >= 0) {
            lua_pushstring(L, "payload");
            lua_pushlstring(L, req.payload.buf, req.payload.len);
            lua_settable(L, -3);
        }
    } else {
        luaL_argerror(L, 1, "Could not parse coap packet from string");
    }
    return 1;
}

int lua_wrap_response_with_code(lua_State *L, const char *code) {
    bool wrapped = false;
    int nArgs = lua_gettop(L);

    if (!lua_istable(L, 1)) {
        wrapped = true;
        lua_newtable(L);
    }
    lua_pushstring(L, "code");
    lua_pushstring(L, code);
    lua_settable(L, 1);

    if (wrapped && nArgs > 0) {
        if (!lua_isstring(L, 1)) {
            luaL_argerror(L, 1, "First arg must be a string or a table");
            return 1;
        }
        lua_pushstring(L, "payload");
        lua_pushvalue(L, 1);
        lua_settable(L, -3);
    }
    return 1;
}


#define STATUS_FUNC(C) static int reply_##C(lua_State *L) { return lua_wrap_response_with_code(L, #C); }

STATUS_FUNC(empty);
STATUS_FUNC(created);
STATUS_FUNC(deleted);
STATUS_FUNC(valid);
STATUS_FUNC(changed);
STATUS_FUNC(content);
STATUS_FUNC(continue);

STATUS_FUNC(bad_request);
STATUS_FUNC(unauthorized);
STATUS_FUNC(bad_option);
STATUS_FUNC(forbidden);
STATUS_FUNC(not_found);
STATUS_FUNC(method_not_allowed);
STATUS_FUNC(not_accpetable);
STATUS_FUNC(request_incomplete);
STATUS_FUNC(precondition_failed);
STATUS_FUNC(request_too_large);
STATUS_FUNC(unsupported_format);

STATUS_FUNC(internal_error);
STATUS_FUNC(not_implemented);
STATUS_FUNC(bad_gateway);
STATUS_FUNC(service_unavailable);
STATUS_FUNC(gateway_timeout);
STATUS_FUNC(proxy_not_supported);




static const struct luaL_Reg coap_funcs[] = {
    // { "resource", registerResource },
    { "decode", lua_coap_parse}, // Parses a string into a table with extracted fields. 
    { "encode", lua_coap_serialise}, // Parses a string into a table with extracted fields. 

    // Convenience functions for creating responses with a certain code. 
    { "emtpy", reply_empty },
    { "created", reply_created },
    { "deleted", reply_deleted },
    { "valid", reply_valid },
    { "changed", reply_changed },
    { "content", reply_content },
    { "continue", reply_continue },

    { "bad_request", reply_bad_request },
    { "unauthorized", reply_unauthorized },
    { "bad_option", reply_bad_option },
    { "forbidden", reply_forbidden },
    { "not_found", reply_not_found },
    { "method_not_allowed", reply_method_not_allowed },
    { "not_accpetable", reply_not_accpetable },
    { "request_incomplete", reply_request_incomplete },
    { "precondition_failed", reply_precondition_failed },
    { "request_too_large", reply_request_too_large },
    { "unsupported_format", reply_unsupported_format },

    { "internal_error", reply_internal_error },
    { "not_implemented", reply_not_implemented },
    { "bad_gateway", reply_bad_gateway },
    { "service_unavailable", reply_service_unavailable },
    { "gateway_timeout", reply_gateway_timeout },
    { "proxy_not_supported", reply_proxy_not_supported },


    // Block option value
    { "block_opt", coap_block_opt},
    { NULL, NULL }
};


int luaopen_coap(lua_State *L)
{
    luaL_newlib(L, coap_funcs);
    return 1;
}