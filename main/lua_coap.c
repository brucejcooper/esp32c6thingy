#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <freertos/semphr.h>
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
#include "lua_system.h"

#define TAG "coap"

typedef struct {
    lua_State *L;
    int fnRef;
} coap_resource_context_t;


static const char *messageTypeStrings[] = {
    "confirmable",
    "non_confirmable",
    "ack",
    "reset",
};

static const char *RESPONSE_BODY_KEY = "response_body";
static const char *RESPONSE_CODE_KEY = "response_code";

ccpeed_err_t coap_set_body(lua_State *L, int argIndex, otMessage *response) {
    uint8_t *body = NULL;
    size_t bodysz;
    otError error = OT_ERROR_NONE;
    uint8_t buf[2048];
    CborEncoder enc;
    CborError err;


    switch (lua_type(L, argIndex)) {
        case LUA_TSTRING:
            body = (uint8_t *) lua_tolstring(L, argIndex, &bodysz);
            ESP_LOGD(TAG, "String body %.*s", bodysz, (char *) body);
            otCoapMessageAppendContentFormatOption(response, OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN);
            break;
        case LUA_TTABLE:
            ESP_LOGD(TAG, "Table body");

            // TODO auto-serialise the message using CBOR.
            cbor_encoder_init(&enc, buf, sizeof(buf), 0);
            // Make a copy on the top of the stack. 
            lua_pushvalue(L, argIndex);
            err= lua_to_cbor(L, &enc);
            if (err != CborNoError) {
                ESP_LOGE(TAG, "Error writing response to CBOR");
                return CCPEED_ERROR_INVALID;
            } else {
                body = buf;
                bodysz = enc.data.ptr-buf;
                ESP_LOGD(TAG, "Encoded to CBOR in %d bytes", bodysz);
                ESP_LOG_BUFFER_HEXDUMP(TAG, buf, bodysz, ESP_LOG_VERBOSE);
            }
            lua_pop(L, 1);
            otCoapMessageAppendContentFormatOption(response, OT_COAP_OPTION_CONTENT_FORMAT_CBOR);
            break;
        case LUA_TNIL:
            ESP_LOGI(TAG, "Body is nil");
            break;
        default:
            ESP_LOGE(TAG, "Unknown repsonse body type %d", lua_type(L, argIndex));
            return CCPEED_ERROR_INVALID;
            break;
    }

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

    ccpeed_err_t cerr = coap_set_body(L, 2, response);
    otCoapMessageSetCode(response, cerr == CCPEED_NO_ERR ? OT_COAP_CODE_CONTENT : OT_COAP_CODE_INTERNAL_ERROR);
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


static bool is_field_set(lua_State *L, int index, const char *name) {
    lua_getfield(L, index, name);
    bool set = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return set;

}


static int post_handler(lua_State *L) {
    // Args are 
    // 1. the status (boolean) - True if it worked, false if it errored.
    // 2. the result (any)
    // 3. the context (a message)
    lua_getfield(L, 3, "_sem");
    SemaphoreHandle_t sem = (SemaphoreHandle_t) lua_touserdata(L, -1);
    lua_pop(L, 1 );

    // Push the response body if one was returned
    if (!lua_isnil(L, 2) || !is_field_set(L, 3, RESPONSE_BODY_KEY)) {
        lua_pushstring(L, RESPONSE_BODY_KEY);
        lua_pushvalue(L, 2);
        lua_settable(L, 3);
    }


    // Set the respone code, if it hasn't already been set.
    if (lua_toboolean(L, 1)) {
        if (!is_field_set(L, 3, RESPONSE_CODE_KEY)) {
            lua_pushstring(L, RESPONSE_CODE_KEY);
            lua_pushinteger(L, OT_COAP_CODE_CONTENT);
            lua_settable(L, 3);
        }
    } else {
        // Errors are always represented as a 500.
        lua_pushstring(L, RESPONSE_CODE_KEY);
        lua_pushinteger(L, OT_COAP_CODE_INTERNAL_ERROR);
        lua_settable(L, 3);
    }

    xSemaphoreGive(sem);
    return 0;
}




/**
 * Called when a registered resource is called from COAP.
 */
static void coap_call_handler(void *aContext, otMessage *request_message, const otMessageInfo *aMessageInfo) {
    otMessage *response = NULL;
    otError error = OT_ERROR_NONE;
    otInstance *instance = esp_openthread_get_instance();
    coap_resource_context_t *ctx = (coap_resource_context_t *) aContext;
    lua_State *L = ctx->L;

    ESP_LOGD(TAG, "resource COAP handler called");

    lua_lock_mine();
    
    // Create a message object - We do it here first so that we'll still have it after the function call. 
    lua_newtable(L);
    lua_pushstring(L, "code");
    lua_pushinteger(L, otCoapMessageGetCode(request_message));
    lua_settable(L, -3);

    lua_pushstring(L, "message_id");
    lua_pushinteger(L, otCoapMessageGetMessageId(request_message));
    lua_settable(L, -3);

    lua_pushstring(L, "token");
    lua_pushlstring(L, (char *) otCoapMessageGetToken(request_message), otCoapMessageGetTokenLength(request_message));
    lua_settable(L, -3);

    lua_pushstring(L, "path");
    lua_push_options_from_coap(L, request_message, OT_COAP_OPTION_URI_PATH);
    lua_settable(L, -3);


    lua_pushstring(L, "query");
    lua_push_options_from_coap(L, request_message, OT_COAP_OPTION_URI_QUERY);
    lua_settable(L, -3);

    lua_pushstring(L, "observe");
    lua_pushinteger(L, extractIntOption(request_message, OT_COAP_OPTION_OBSERVE));
    lua_settable(L, -3);

    lua_pushstring(L, "type");
    lua_pushstring(L, messageTypeStrings[otCoapMessageGetType(request_message)]);
    lua_settable(L, -3);

    lua_pushstring(L, "acknowledged");
    lua_pushboolean(L, false);
    lua_settable(L, -3);

    // To be able to asynchronously reply, we need a semaphore
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    lua_pushstring(L, "_sem");
    lua_pushlightuserdata(L, sem);
    lua_settable(L, -3);



    // get the function reference back onto the stack
    ESP_LOGD(TAG, "Calling coap handler id %d", ctx->fnRef);
    lua_pushcfunction(L, start_coro);              // call start_coro
    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->fnRef); // Argument 1 - The function to call
    lua_pushvalue(L, -3);                          // Argument 2 - The argument to send to the function when run for the first time. ref to the message object.
    lua_pushcfunction(L, post_handler);            // Argument 3 - post-completion callback fn.
    lua_pushvalue(L, -2);                          // Argument 4 - callback context - the message object
    
    int r = lua_pcall(L, 4, 0, 0);
    if (r) {
        lua_report_error(L, r, "Couldn't start handler co-routine");
    } else {
        // the message is on top of the stack. That is bad for confusing the coro-runner, so we stash it instead. 
        int msgRef = luaL_ref(L, LUA_REGISTRYINDEX);

        // openthread will destroy the message Context and request when this call completes.
        // We don't want to do that, so we'll wait for the co-routine here. 
        lua_unlock_mine();
        if (xSemaphoreTake(sem, pdMS_TO_TICKS(1750)) == pdTRUE) {
        } else {
            // Timed out
            ESP_LOGW(TAG, "Timed out!");
        }
        lua_lock_mine();


        // Send a response message.
        // Now get the message ref back
        lua_rawgeti(L, LUA_REGISTRYINDEX, msgRef);
        luaL_unref(L, LUA_REGISTRYINDEX, msgRef); // We don't need it any more.


        lua_getfield(L, -1, "response_code");
        int response_code = lua_tointeger(L, -1);
        lua_pop(L, 1);


        response = otCoapNewMessage(instance, NULL);
        if (response == NULL) {
            goto cleanup;
        }

        bool needsAck = otCoapMessageGetType(request_message) == OT_COAP_TYPE_CONFIRMABLE;
        if (needsAck) {
            error = otCoapMessageInitResponse(response, request_message,  OT_COAP_TYPE_ACKNOWLEDGMENT, response_code);
        } else {
            // We've already acknowledged the message, so we send a new independent message with the same token.
            otCoapMessageInit(response,  OT_COAP_TYPE_NON_CONFIRMABLE, response_code);
            error = otCoapMessageSetToken(response, otCoapMessageGetToken(request_message), otCoapMessageGetTokenLength(request_message));
        }
        if (error != OT_ERROR_NONE) {
            ESP_LOGE(TAG, "Error initialising response message");
            goto cleanup;
        }

        lua_getfield(L, -1, RESPONSE_BODY_KEY);
        ESP_LOGD(TAG, "Body is a %s", lua_type_str(L, -1));
        ccpeed_err_t cerr = coap_set_body(L, -1, response);
        lua_pop(L, 1);

        if (cerr != CCPEED_NO_ERR) {
            ESP_LOGE(TAG, "Unable to encode response");
            otCoapMessageSetCode(response, OT_COAP_CODE_INTERNAL_ERROR);
        }
        ESP_LOGD(TAG, "Sending Response");
        error = otCoapSendResponse(instance, response, aMessageInfo);
cleanup:
        if (error != OT_ERROR_NONE && response != NULL) {
            otMessageFree(response);
        }
    }
    vSemaphoreDelete(sem);
    lua_pop(L, 1); // The message object.
    lua_unlock_mine();
    ESP_LOGD(TAG, "Completed handler");
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


static int sendAcknowledgement(lua_State *L) {
    return 0;
}

static int sendReset(lua_State *L) {
    return 0;
}

static int sendObservation(lua_State *L) {
    return 0;
}




static const struct luaL_Reg coap_funcs[] = {
    { "resource", registerResource },
    { "reply", sendReply },
    { "ack", sendAcknowledgement },
    { "reset", sendReset },
    { "send_observation", sendObservation },
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