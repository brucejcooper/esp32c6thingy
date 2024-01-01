#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include <openthread/coap.h>
#include <openthread/instance.h>
#include <esp_openthread.h>
#include <string.h>
#include "lua_system.h"
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include "ccpeed_err.h"
#include "cbor_helpers.h"

#define TAG "coap"

typedef struct {
    lua_State *L;
    int fnRef;
} coap_resource_context_t;





ccpeed_err_t coap_set_body(lua_State *L, int argIndex, otMessage *response) {
    uint8_t *body = NULL;
    size_t bodysz;
    otCoapCode response_code = OT_COAP_CODE_EMPTY;
    otError error = OT_ERROR_NONE;
    uint8_t buf[2048];
    CborEncoder enc;
    CborError err;

    if (lua_istable(L, argIndex)) {
        lua_getfield(L, argIndex, "body");
        switch (lua_type(L, -1)) {
            case LUA_TSTRING:
                ESP_LOGD(TAG, "String body");
                body = (uint8_t *) lua_tolstring(L, -1, &bodysz);
                response_code = OT_COAP_CODE_CONTENT;
                break;
            case LUA_TTABLE:
                // TODO auto-serialise the message using CBOR.
                cbor_encoder_init(&enc, buf, sizeof(buf), 0);

                err= lua_to_cbor(L, &enc);
                if (err != CborNoError) {
                    ESP_LOGE(TAG, "Error writing response to CBOR");
                    response_code = OT_COAP_CODE_INTERNAL_ERROR;
                } else {
                    body = buf;
                    bodysz = enc.data.ptr-buf;
                    response_code = OT_COAP_CODE_CONTENT;
                    ESP_LOGD(TAG, "Encoded to CBOR in %d bytes", bodysz);
                    ESP_LOG_BUFFER_HEXDUMP(TAG, buf, bodysz, ESP_LOG_VERBOSE);
                }
                break;
            case LUA_TNIL:
                ESP_LOGD(TAG, "Body is nil");
                break;
            default:
                ESP_LOGE(TAG, "Unknown repsonse body type %d", lua_type(L, -1));
                break;
        }
        lua_pop(L, 1);

        lua_getfield(L, argIndex, "code");
        if (lua_isnumber(L, -1)) {
            response_code = lua_tointeger(L, -1);
        } else if (!lua_isnil(L, -1)) {
            ESP_LOGE(TAG, "Code must be absent or a valid COAP code");
            return CCPEED_ERROR_INVALID;

        }
        lua_pop(L, 1);

    } else if (!lua_isnil(L, argIndex)) {
        ESP_LOGE(TAG, "Expected table response, or none");
        return CCPEED_ERROR_INVALID;
    }

    otCoapMessageSetCode(response, response_code);
    if (body) {
        error = otCoapMessageSetPayloadMarker(response);
        if (error != OT_ERROR_NONE) {
            return CCPEED_ERROR_NOMEM;
        }
        error = otMessageAppend(response, body, bodysz);
        if (error != OT_ERROR_NONE) {
            return CCPEED_ERROR_NOMEM;
        }
    }
    return CCPEED_NO_ERR;
}


int sendReply(lua_State *L) {
    otInstance *instance = esp_openthread_get_instance();
    // Two arguments - First is the request we passed to the handler.  The second is the response.
    otMessage *response = NULL;
    otError error = OT_ERROR_NONE;

    // get the msgInfo
    lua_getfield(L, 1, "msgInfo");
    otMessageInfo *aMessageInfo = (otMessageInfo *) lua_touserdata(L, -1);
    lua_getfield(L, 1, "token");
    size_t tokensz;
    const uint8_t *token = (const uint8_t *) lua_tolstring(L, -1, &tokensz);



    response = otCoapNewMessage(instance, NULL);
	if (response == NULL) {
		goto end;
	}

    otCoapMessageInit(response, OT_COAP_TYPE_CONFIRMABLE, OT_COAP_CODE_CONTENT);
    // Copy the token across from the request
    otCoapMessageSetToken(response, token, tokensz);

    coap_set_body(L, 2, response);
    ESP_LOGD(TAG, "sending deferred response with status %d", otCoapMessageGetCode(response));
    error = otCoapSendResponse(instance, response, aMessageInfo);
    if (error != OT_ERROR_NONE) {
        luaL_error(L, "Error sending coap response");
		otMessageFree(response);
    }
end:
    lua_pop(L, 2);
    return 0;
}



/**
 * Creates a new table on the stack as an array, with items equal to all entries of the requested option in the message.
*/
static void lua_push_options_from_coap(lua_State *L, otMessage *msg, uint16_t optId) {
    otCoapOptionIterator iter;
    char buf[500];

    lua_newtable(L);

    otCoapOptionIteratorInit(&iter, msg);
    int i = 1;
    const otCoapOption *opt = otCoapOptionIteratorGetFirstOptionMatching(&iter, optId);
    // Create a table into which we will place all the items. 
    while (opt) {
        if (opt->mLength > sizeof(buf)) {
            ESP_LOGE(TAG, "Option too big");
        } else {
            lua_pushinteger(L, i++);
            assert(otCoapOptionIteratorGetOptionValue(&iter, buf) == OT_ERROR_NONE);
            lua_pushlstring(L, buf, opt->mLength);
            ESP_LOGD(TAG, "Got option %d (len %d) \"%.*s\"", optId, opt->mLength, opt->mLength, (char *) buf);                    
            lua_settable(L, -3);
        }
        opt = otCoapOptionIteratorGetNextOptionMatching(&iter, optId);
    }
}

static int32_t extractIntOption(otMessage *msg, uint16_t optId) {
    uint64_t val;

    otCoapOptionIterator iter;
    otCoapOptionIteratorInit(&iter, msg);
    const otCoapOption *opt = otCoapOptionIteratorGetFirstOptionMatching(&iter, optId);
    if (opt) {
        assert(otCoapOptionIteratorGetOptionValue(&iter, &val) == OT_ERROR_NONE);
        return (int32_t) val;
    } else {
        return -1;
    }
}


/**
 * Called when a registered resource is called from COAP.
 */
static void coap_call_handler(void *aContext, otMessage *request_message, const otMessageInfo *aMessageInfo) {
    ESP_LOGD(TAG, "resource COAP handler called");
    coap_resource_context_t *ctx = (coap_resource_context_t *) aContext;
    otMessage *response = NULL;
    otError error = OT_ERROR_NONE;
    otInstance *instance = esp_openthread_get_instance();


    // get the function reference back onto the stack
    ESP_LOGD(TAG, "Calling coap handler id %d", ctx->fnRef);
    assert(lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->fnRef));

    // Create an argument for the function. This will be a table version of the request_message
    lua_newtable(ctx->L);
    lua_pushstring(ctx->L, "code");
    lua_pushinteger(ctx->L, otCoapMessageGetCode(request_message));
    lua_settable(ctx->L, -3);

    lua_pushstring(ctx->L, "message_id");
    lua_pushinteger(ctx->L, otCoapMessageGetMessageId(request_message));
    lua_settable(ctx->L, -3);

    lua_pushstring(ctx->L, "token");
    lua_pushlstring(ctx->L, (char *) otCoapMessageGetToken(request_message), otCoapMessageGetTokenLength(request_message));
    lua_settable(ctx->L, -3);

    lua_pushstring(ctx->L, "path");
    lua_push_options_from_coap(ctx->L, request_message, OT_COAP_OPTION_URI_PATH);
    lua_settable(ctx->L, -3);


    lua_pushstring(ctx->L, "query");
    lua_push_options_from_coap(ctx->L, request_message, OT_COAP_OPTION_URI_QUERY);
    lua_settable(ctx->L, -3);

    lua_pushstring(ctx->L, "observe");
    lua_pushinteger(ctx->L, extractIntOption(request_message, OT_COAP_OPTION_OBSERVE));
    lua_settable(ctx->L, -3);

    // To be able to asynchronously reply, we need a msgInfo. Instead of meticulously coping it out, we just make a C copy.
    lua_pushstring(ctx->L, "msgInfo");
    otMessageInfo *msgInfoCopy = (otMessageInfo *) lua_newuserdata(ctx->L, sizeof(otMessageInfo));
    memcpy(msgInfoCopy, aMessageInfo, sizeof(otMessageInfo));
    lua_settable(ctx->L, -3);


    response = otCoapNewMessage(instance, NULL);
	if (response == NULL) {
		goto end;
	}

    bool isConfirmable = otCoapMessageGetType(request_message) == OT_COAP_TYPE_CONFIRMABLE;
    error = otCoapMessageInitResponse(response, request_message,  isConfirmable ? OT_COAP_TYPE_ACKNOWLEDGMENT : OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_EMPTY);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Error initialising response message");
        goto end;
    }

    // Call the handler, expecting one response (it may be nil)
    int r = lua_pcall(ctx->L, 1, 1, 0);
    if (r) {
        otCoapMessageSetCode(response, OT_COAP_CODE_INTERNAL_ERROR);
        size_t errsz;
        const char *errMsg = lua_tolstring(ctx->L, -1, &errsz);
        error = otCoapMessageSetPayloadMarker(response);
        if (error != OT_ERROR_NONE) {
            goto end;
        }
        error = otMessageAppend(response, errMsg, errsz);
        if (error != OT_ERROR_NONE) {
            goto end;
        }
        lua_report_error(ctx->L, r, "COAP Hanlder failure");
    } else {
        coap_set_body(ctx->L, -1, response);
    }

    if (isConfirmable) {
        ESP_LOGD(TAG, "Sending repsonse with code 0x%02x", otCoapMessageGetCode(response));
	    error = otCoapSendResponse(instance, response, aMessageInfo);

        if (error != OT_ERROR_NONE) {
            ESP_LOGE(TAG, "Error sending response");
        }
        return; // Skip the cleanup.
    } else {
        ESP_LOGI(TAG, "Not sending response, as it wasn't a confirmable request");
    }
end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}
}



static int registerResource(lua_State *L){
    // We expect two parameters, a path and a handler function
    size_t sz;
    const char *path = luaL_checklstring(L, 1, &sz);

    // Handler
    if (!lua_isfunction(L, -1)) {
        luaL_argerror(L, 1, "Expected 2nd argument to be a function");
        return 1;
    }
    // Take a reference to the handler function, as we will use this when a call comes in to create a co-routine.
    int fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
    ESP_LOGI(TAG, "Registered coap handler for resource '%s' fun with id %d", path, fnRef);
    // Pop off the arguments, as we no longer need them. 
    lua_pop(L, 2);

    // Now create the otCoap resource
    otCoapResource *resource = (otCoapResource *) malloc(sizeof(otCoapResource));
    assert(resource);
    coap_resource_context_t *ctx = (coap_resource_context_t *) malloc(sizeof(coap_resource_context_t));
    assert(ctx);
    ctx->fnRef = fnRef;
    ctx->L = L;
    resource->mContext = ctx;
    resource->mHandler = coap_call_handler;
    resource->mNext = NULL;
    resource->mUriPath = strdup(path);
    assert(resource->mUriPath);

    otInstance *instance = esp_openthread_get_instance();
    otCoapAddResource(instance, resource);
    return 0;
}


static const struct luaL_Reg coap_funcs[] = {
    { "resource", registerResource },
    { "reply", sendReply },
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