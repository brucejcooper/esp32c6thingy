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
#include "cbor_helpers.h"
#include "lua_system.h"

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
    lstr_arr_t if_none_match;
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
} coap_response_t;


static int coapHandlerFuncitonRef = LUA_NOREF;


// static const char *messageTypeStrings[] = {
//     "confirmable",
//     "non_confirmable",
//     "ack",
//     "reset",
// };



// static TaskHandle_t handlerTask = NULL;





// static int registerResource(lua_State *L){
//     // We expect two parameters, a path and a handler function
//     size_t sz;
//     const char *path = luaL_checklstring(L, 1, &sz);

//     // Handler
//     if (!lua_isfunction(L, -1)) {
//         luaL_argerror(L, 1, "Expected 2nd argument to be a function");
//         return 1;
//     }
//     // Take a reference to the handler function, as we will use this when a call comes in to create a co-routine.
//     int fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
//     ESP_LOGI(TAG, "Registered coap handler for resource '%s' fun with id %d", path, fnRef);
//     // Pop off the arguments, as we no longer need them. 
//     lua_pop(L, 2);

//     // Now create the otCoap resource
//     otCoapResource *resource = (otCoapResource *) malloc(sizeof(otCoapResource));
//     assert(resource);
//     resource->mContext = (void *) fnRef;
//     resource->mHandler = coap_call_handler;
//     resource->mNext = NULL;
//     resource->mUriPath = strdup(path);
//     assert(resource->mUriPath);

//     otInstance *instance = esp_openthread_get_instance();
//     otCoapAddResource(instance, resource);
//     return 0;
// }










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
    lstr_arr_init(&pkt->if_none_match, 0);
    lstr_arr_init(&pkt->uri_query, 0);
    pkt->payload.len = 0;
    pkt->code = OT_COAP_CODE_EMPTY;
}

void coap_packet_free(coap_packet_t *pkt) {
    lstr_arr_free(&pkt->uri_path);
    lstr_arr_free(&pkt->uri_query);
    lstr_arr_free(&pkt->if_match);
    lstr_arr_free(&pkt->if_none_match);
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

    // Parse the options.
    otCoapOptionType opt = 0;
    ssize_t remaining = numRead+buf-ptr;
    while (remaining > 0 && *ptr != 0xFF) {
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
                break;
            case OT_COAP_OPTION_IF_MATCH:
                lstr_arr_append(&pkt->if_match, (char *) ptr, opt_len);
                break;
            case OT_COAP_OPTION_IF_NONE_MATCH:
                lstr_arr_append(&pkt->if_none_match, (char *) ptr, opt_len);
                break;
            case OT_COAP_OPTION_E_TAG:
                pkt->etag.buf = (char *) ptr;
                pkt->etag.len = opt_len;
                break;
            case OT_COAP_OPTION_BLOCK1:
                if (opt_len < 1 || opt_len > 3) {
                    ESP_LOGE(TAG, "Invalid Block2 length");
                    return false;
                }
                uint32_t block1Val = parseIntOpt(ptr, opt_len);
                pkt->block1_sz = block1Val & 0x07;
                pkt->block1_id = block1Val >> 4;
                pkt->block1_more = block1Val & 0x08 ? true : false;
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
    if (remaining > 0) {
        ptr++;
        remaining--;
    } else if (remaining < 0) {
        return false;
    }
    pkt->payload.buf = (char *) ptr;
    pkt->payload.len = remaining;
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



static ccpeed_err_t coap_response_append_option(coap_response_t *i, const otCoapOptionType type, uint8_t *val, size_t val_len) {
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
        *i->ptr++ = exDeltaSz >> 8;
    }
    if (exDeltaSz > 1) {
        *i->ptr++ = exDeltaSz & 0xFF;
    }
    if (exValLenSz == 2) {
        *i->ptr++ = exValLenSz >> 8;
    }
    if (exValLenSz > 1) {
        *i->ptr++ = exValLenSz & 0xFF;
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

    if (numBytes == 0) {
        // This isn't strictly necessary, but depending on other functions not doing stuff is bad practice.
        return coap_response_append_option(i, type, NULL, 0);
    }

    while (val) {
        *ptr-- = val & 0xFF;
        val >>= 8;
        numBytes++;
    }
    return coap_response_append_option(i, type, ptr+1, numBytes);
}


static inline void coap_response_append_content_marker(coap_response_t *i) {
    *i->ptr++ = 0xFF;
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


    if (i->req.block2_id >= 0) {
        lua_pushstring(L, "block2");
        lua_newtable(L);
        lua_pushstring(L, "id");
        lua_pushinteger(L, i->req.block2_id);
        lua_settable(L, -3);

        lua_pushstring(L, "size");
        lua_pushinteger(L, 1 << (4 + i->req.block2_sz));
        lua_settable(L, -3);

        lua_pushstring(L, "size_ex");
        lua_pushinteger(L, i->req.block2_sz);
        lua_settable(L, -3);

        lua_settable(L, -3);
    }


    // The res field in the request is a response object, which can be used to set
    // various fields that will be used to shape the CoAP response
    lua_pushstring(L, "res");
    lua_newtable(L);
    lua_settable(L, -3);


    // lua_pushstring(L, "query");
    // lua_push_lstr_arr(L, &i->req.uri_query);
    // lua_settable(L, -3);

    // lua_pushstring(L, "observe");
    // lua_pushinteger(L, extractIntOption(request_message, OT_COAP_OPTION_OBSERVE));
    // lua_settable(L, -3);

    // lua_pushstring(L, "type");
    // lua_pushstring(L, messageTypeStrings[otCoapMessageGetType(request_message)]);
    // lua_settable(L, -3);

    lua_pushstring(L, "acknowledged");
    lua_pushboolean(L, false);
    lua_settable(L, -3);
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
    CborEncoder enc;
    CborError err;
    ccpeed_err_t cerr;

    char *errmsg = NULL;
    size_t errmsglen = 0;

    ESP_LOGD(TAG, "Handler respone is %d %s %s", lua_toboolean(L, 1), lua_type_str(L, 2), lua_type_str(L, 2));
    
    // Encode any errors or return values. 
    if (!lua_toboolean(L, 1)) {
        errmsg = (char *) lua_tolstring(L, 2, &errmsglen);
        goto error;
    }

    if (i->payload_started && !lua_isnil(L, 2)) {
        // This is valid, but will be weird
        ESP_LOGW(TAG, "co-routine sent content and also returned a value. Both will be concatenated");
    }        
    switch (lua_type(L, 2)) {
        case LUA_TSTRING:
            // coap_response_try_set_content_format(i, OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN);
            size_t bodysz;
            uint8_t *body = (uint8_t *) lua_tolstring(L, 2, &bodysz);
            cerr = coap_response_append(i, body, bodysz);
            if (cerr != CCPEED_NO_ERR) {
                errmsg = "No space in buffer";
                goto error;
            }
            break;
        case LUA_TTABLE:
            ESP_LOGD(TAG, "Table body");
            if (coap_response_try_set_content_format(i, OT_COAP_OPTION_CONTENT_FORMAT_CBOR)) {
                // We were able to append it, which means we now need a payload marker.
                coap_response_append_content_marker(i);
            }

            // encode the table using CBOR directly into the output buffer.
            cbor_encoder_init(&enc, i->ptr, coap_response_space_remaining(i), 0);
            // Make a copy of the value on the top of the stack, as lua_to_cbor only works on the top.
            lua_pushvalue(L, 2);
            err= lua_to_cbor(L, &enc);
            if (err != CborNoError) {
                errmsg = "Could not serialise table response to CBOR";
                goto error;
            } else {
                size_t serlen = enc.data.ptr-i->ptr;
                i->ptr += serlen;
            }
            lua_pop(L, 1);
            break;
        case LUA_TNIL:
            // Nothing to append.
            break;
        default:
            ESP_LOGE(TAG, "Unknown repsonse body type %d", lua_type(L, 2));
            errmsg = "Unknown coroutine response type";
            goto error;
    }
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
    // Now send the response.
    otInstance *instance = esp_openthread_get_instance();
    otMessage *respMsg = otUdpNewMessage(instance, NULL);
    int bufLen = i->ptr - i->response_buf;
    otMessageAppend(respMsg, i->response_buf, bufLen);
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
    interaction->response_buf[1] = interaction->req.code == OT_COAP_CODE_GET ? OT_COAP_CODE_CONTENT : OT_COAP_CODE_CHANGED;
    interaction->response_buf[2] = interaction->req.message_id >> 8;
    interaction->response_buf[3] = interaction->req.message_id & 0xFF;
    memcpy(interaction->response_buf+4, interaction->req.token.buf, interaction->req.token.len);
    coap_response_reset_response(interaction);


    // The respone is now in a state where it can be processed and a response sent.  Schedule a lua coro to do the work.

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






static const struct luaL_Reg coap_funcs[] = {
    // { "resource", registerResource },
    { "set_coap_handler", set_coap_handler },
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