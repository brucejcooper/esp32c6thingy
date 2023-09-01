/* CoAP server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
 * WARNING
 * libcoap is not multi-thread safe, so only this thread must make any coap_*()
 * calls.  Any external (to this thread) data transmitted in/out via libcoap
 * therefore has to be passed in/out by xQueue*() via this thread.
 */

#include <string.h>
#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mdns.h"

#include "nvs_flash.h"

#include "connect.h"
#include "dali.h"

#include <driver/uart.h>

#include <cbor.h>
#include "openthread/cli.h"
#include "openthread/instance.h"
#include "esp_openthread.h"
#include <esp_openthread_cli.h>
#include "esp_openthread_types.h"
#include "esp_openthread_netif_glue.h"
#include "openthread/logging.h"
#include <openthread/platform/settings.h>
#include <openthread/ip6.h>
#include <openthread/dataset.h>
#include <openthread/thread.h>
#include <openthread/icmp6.h>
#include <openthread/coap.h>
#include "openthread/tasklet.h"
#include "esp_vfs_eventfd.h"


#ifndef CONFIG_COAP_SERVER_SUPPORT
#error COAP_SERVER_SUPPORT needs to be enabled
#endif /* COAP_SERVER_SUPPORT */



typedef enum { 
    DEVICE_ASPECT_TYPE_LIGHT = 1, 
    DEVICE_ASPECT_TYPE_PUSH_BUTTON = 2, 
    DEVICE_ASPECT_TYPE_THERMOSTAT = 3, 
    DEVICE_ASPECT_TYPE_TEMPERATURE_HUMIDITY_SENSOR = 4, 
    DEVICE_ASPECT_TYPE_PROTOCOL_BRIDGE = 5, 
} device_aspect_type_t;

#define MAX_ASPECT_COUNT 5

typedef struct {
    uint8_t serial[20];
    size_t len;
} device_serial_t;

typedef struct {
    device_serial_t target;
    device_aspect_type_t aspect;
    uint32_t serviceId;
    uint8_t *arguments; // CBOR encoded array, or NULL for none.
} device_action_t;


typedef enum {
    LIGHT_ATTR_IS_ON = 0,
    LIGHT_ATTR_LEVEL = 1,
    LIGHT_ATTR_MIN_LEVEL = 2,
    LIGHT_ATTR_MAX_LEVEL = 3,
} light_attrid_t;



typedef struct {
    device_aspect_type_t type;
    bool is_on;
    uint8_t level;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t power_on_level;
    uint8_t lightType;
    uint16_t group_membership;

} light_aspect_t;

typedef enum {
    LIGHT_SERVICEID_SET_ATTR = 0, // Expects map of attribute ID to value.
    LIGHT_SERVICEID_TURN_ON = 1,
    LIGHT_SERVICEID_TURN_OFF = 2,
    LIGHT_SERVICEID_TOGGLE = 3,
    LIGHT_SERVICEID_APPLY_BRIGHTNESS_DELTA = 4, // Expects one argument of delta amount (sint)
} light_serviceid_t;

typedef enum {
    PUSH_BUTTON_EVENT_PRESS = 1,
    PUSH_BUTTON_EVENT_RELEASE = 2,
    PUSH_BUTTON_EVENT_LONG_PRESS = 3,
    PUSH_BUTTON_EVENT_LONG_PRESS_REPEAT = 4,
} push_button_eventid_t;




typedef struct {
    push_button_eventid_t event;
    device_action_t action;
} push_button_event_t;

#define MAX_PUSH_BUTTON_EVENTS 5



typedef struct {
    device_aspect_type_t type;
    unsigned int num_events;
    push_button_event_t events[MAX_PUSH_BUTTON_EVENTS];
} push_button_aspect_t;



typedef union {
    device_aspect_type_t type;
    light_aspect_t light;
    push_button_aspect_t push_button;
} device_aspect_t;


typedef struct device_t {
    device_serial_t serial;
    device_aspect_t aspects[MAX_ASPECT_COUNT];
    uint32_t numAspects;
} device_t;



#define MAX_DEVICES (64 + 1 + 16)
size_t num_devices = 0;
device_t devices[MAX_DEVICES];

mdns_ip_addr_t broadcast_device_ips;


/* The examples use simple Pre-Shared-Key configuration that you can set via
   'idf.py menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_COAP_PSK_KEY "some-agreed-preshared-key"

   Note: PSK will only be used if the URI is prefixed with coaps://
   instead of coap:// and the PSK must be one that the server supports
   (potentially associated with the IDENTITY)
*/
#define EXAMPLE_COAP_PSK_KEY CONFIG_EXAMPLE_COAP_PSK_KEY

/* The examples use CoAP Logging Level that
   you can set via 'idf.py menuconfig'.

   If you'd rather not, just change the below entry to a value
   that is between 0 and 7 with
   the config you want - ie #define EXAMPLE_COAP_LOG_DEFAULT_LEVEL 7
*/
#define EXAMPLE_COAP_LOG_DEFAULT_LEVEL CONFIG_COAP_LOG_DEFAULT_LEVEL

const static char *TAG = "CoAP_server";

static void testResourceHandler(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo);

otCoapResource testResource = {
    .mContext = NULL,
    .mHandler = testResourceHandler,
    .mNext = NULL,
    .mUriPath = "d"
};

static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));

    return netif;
}


static void ipAddressChangeCallback(const otIp6AddressInfo *aAddressInfo, bool aIsAdded, void *aContext) {
    char buf[OT_IP6_ADDRESS_STRING_SIZE];

    if (aIsAdded) {


        otInstance *instance = esp_openthread_get_instance();
        otNetifAddress *addr = (otNetifAddress *) otIp6GetUnicastAddresses(instance);
        
        while (addr) {
            if (addr->mAddressOrigin != OT_ADDRESS_ORIGIN_THREAD) {
                otIp6AddressToString(&(addr->mAddress), buf, OT_IP6_ADDRESS_STRING_SIZE);
                ESP_LOGI(TAG, "External IP Address %s/%d", buf, addr->mPrefixLength);
            }
            addr = addr->mNext;
        }


    } else {
        ESP_LOGI(TAG, "IP Address Removed");
    }
}

static void testResourceHandler(void *aContext, otMessage *request_message, const otMessageInfo *aMessageInfo) {
    ESP_LOGI(TAG, "Test resource called");
    otMessage *response = NULL;
    otError error = OT_ERROR_NONE;

    otInstance *instance = esp_openthread_get_instance();


    response = otCoapNewMessage(instance, NULL);
	if (response == NULL) {
		goto end;
	}

	otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_CONTENT);
	error = otCoapMessageSetToken(
		response, otCoapMessageGetToken(request_message),
		otCoapMessageGetTokenLength(request_message));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		goto end;
	}


	uint8_t *payload = (uint8_t *) "Testing";
	size_t payload_size = 7;

	error = otMessageAppend(response, payload, payload_size);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendResponse(instance, response, aMessageInfo);
	ESP_LOG_BUFFER_HEXDUMP(TAG, payload, payload_size, ESP_LOG_INFO);

end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}

}

/**
 * @brief Determines if two serial numbers are equal.
 * 
 * @param s1 
 * @param s2 
 * @return true 
 * @return false 
 */
static bool serialNumbersMatch(device_serial_t *s1, device_serial_t *s2) {
    ESP_LOGI(TAG, "Comparing");
    ESP_LOG_BUFFER_HEXDUMP(TAG, s1->serial, s1->len, ESP_LOG_INFO);
    ESP_LOG_BUFFER_HEXDUMP(TAG, s2->serial, s2->len, ESP_LOG_INFO);

    return s1->len == s2->len && memcmp(s1->serial, s2->serial, s1->len) == 0;
}


/**
 * @brief Finds a device by its serial Number. 
 * 
 * @param serialNum 
 * @param serialNumLen 
 * @return device_t* a pointer to the device if found, or NULL if not.
 */
static device_t *findDevice(uint8_t *serial, size_t len) {
    device_t *dev = devices;
    for (int remaining = num_devices; remaining > 0; remaining--, dev++) {
        if (len == dev->serial.len && memcmp(serial, dev->serial.serial, len) == 0)
            return dev;
    }
    return NULL;
}

static device_aspect_t *getAspect(device_t *dev, device_aspect_type_t aspectType) {
    for (int i = 0; i < dev->numAspects; i++) {
        if (dev->aspects[i].type == aspectType) {
            return &dev->aspects[i];
        }
    }
    return NULL;
}




static void encodeDeviceSerialAndType(device_t *dev, CborEncoder *encoder) {
    CborEncoder itemEncoder, itemAspectEncoder;

    cbor_encoder_create_array(encoder, &itemEncoder, 2);
    cbor_encode_byte_string(&itemEncoder, dev->serial.serial, dev->serial.len);

    cbor_encoder_create_array(&itemEncoder, &itemAspectEncoder, dev->numAspects);
    for (int j = 0; j < dev->numAspects; j++) {
        cbor_encode_uint(&itemAspectEncoder, dev->aspects[j].type);
    }
    cbor_encoder_close_container(&itemEncoder, &itemAspectEncoder);
    cbor_encoder_close_container(encoder, &itemEncoder);
}


static void encodeLightAspectAttributes(light_aspect_t *light, CborEncoder *encoder) {
    CborEncoder attributeEncoder;
    cbor_encoder_create_map(encoder, &attributeEncoder, 4);

    cbor_encode_uint(&attributeEncoder, LIGHT_ATTR_IS_ON);
    cbor_encode_boolean(&attributeEncoder, light->is_on);

    cbor_encode_uint(&attributeEncoder, LIGHT_ATTR_LEVEL);
    cbor_encode_uint(&attributeEncoder, light->level);

    cbor_encode_uint(&attributeEncoder, LIGHT_ATTR_MIN_LEVEL);
    cbor_encode_uint(&attributeEncoder, light->min_level);

    cbor_encode_uint(&attributeEncoder, LIGHT_ATTR_MAX_LEVEL);
    cbor_encode_uint(&attributeEncoder, light->max_level);

    cbor_encoder_close_container(encoder, &attributeEncoder);
}

static CborError setLightAspectAttribute(light_aspect_t *light, int attributeId, CborValue *val) {
    CborError err = CborNoError;
    int ival;

    switch (attributeId) {
        case LIGHT_ATTR_IS_ON:
            if (!cbor_value_is_boolean(val)) {
                ESP_LOGW(TAG, "Light attribute IS_ON only accepts booleans");
                return CborErrorIllegalType;
            }
            err = cbor_value_get_boolean(val, &light->is_on);
            ESP_LOGI(TAG, "light.is_on set to %d", light->is_on);
            return err;

        case LIGHT_ATTR_LEVEL:
            if (!cbor_value_is_unsigned_integer(val)) {
                ESP_LOGW(TAG, "Light attribute level only accepts unsigned integers");
                return CborErrorIllegalType;
            }
            err = cbor_value_get_int_checked(val, &ival);
            light->level = ival;
            ESP_LOGI(TAG, "light.level set to %d", light->level);
            return err;


        case LIGHT_ATTR_MIN_LEVEL:
            if (!cbor_value_is_unsigned_integer(val)) {
                ESP_LOGW(TAG, "Light attribute min_level only accepts unsigned integers");
                return CborErrorIllegalType;
            }

            err = cbor_value_get_int_checked(val, &ival);
            light->min_level = ival;
            ESP_LOGI(TAG, "light.min_level set to %d", light->min_level);
            return err;


        case LIGHT_ATTR_MAX_LEVEL:
            if (!cbor_value_is_unsigned_integer(val)) {
                ESP_LOGW(TAG, "Light attribute max_level only accepts unsigned integers");
                return CborErrorIllegalType;
            }
            err = cbor_value_get_int_checked(val, &ival);
            light->max_level = ival;
            ESP_LOGI(TAG, "light.max_level set to %d", light->max_level);
            return err;

        default:
            ESP_LOGW(TAG, "Invalid Light Aspect attributeId %d", attributeId);
            return CborErrorImproperValue;
    }
    return err;
}


static int encodeAspectAttributes(device_aspect_t *aspect, uint8_t *buf, size_t max_len) {
    CborEncoder encoder;
    cbor_encoder_init(&encoder, buf, max_len, 0);
    CborEncoder attributeEncoder;

    switch (aspect->type) {
        case DEVICE_ASPECT_TYPE_LIGHT:
            encodeLightAspectAttributes(&aspect->light, &encoder);
            break;
        case DEVICE_ASPECT_TYPE_PUSH_BUTTON:
        case DEVICE_ASPECT_TYPE_THERMOSTAT:
        case DEVICE_ASPECT_TYPE_TEMPERATURE_HUMIDITY_SENSOR:
        case DEVICE_ASPECT_TYPE_PROTOCOL_BRIDGE:
            cbor_encoder_create_map(&encoder, &attributeEncoder, 0);
            cbor_encoder_close_container(&encoder, &attributeEncoder);
            break;
    }
    return encoder.data.ptr-buf;
}


/*
    CborEncoder encoder, listEncoder;
    cbor_encoder_init(&encoder, buf, sizeof(buf), 0);
    cbor_encoder_create_array(&encoder, &listEncoder, numResponses);
    for (int i = 0; i < num_devices; i++) {
        if (snfilter.len == 0 || serialNumbersMatch(&snfilter, &devices[i].serial)) {
            // Each result is a tuple, containing the serial number and type(s) of the device.
            encodeDeviceSerialAndType(devices+i, &encoder);
        }
    }
    cbor_encoder_close_container(&encoder, &listEncoder);

*/

static otCoapCode process_attribute_udpate(device_t *device, device_aspect_t *aspect, uint8_t *buf, size_t len) {

    // Read in the body of the request
    CborParser parser;
    CborValue val, mapIter;
    CborError err = cbor_parser_init(buf, len, 0, &parser, &val);
    len = 0;
    if (err != CborNoError) {                        
        ESP_LOGW(TAG, "Error creating parser %d", err);
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    if (!cbor_value_is_map(&val)) {
        ESP_LOGW(TAG, "Unknown type %d", val.type);
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    size_t num_attrs;
    err = cbor_value_get_map_length(&val, &num_attrs);
    if (err != CborNoError) {                        
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    err = cbor_value_enter_container(&val, &mapIter);
    if (err != CborNoError) {                        
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    ESP_LOGD(TAG, "There are %d attribtues to set", num_attrs);
    for (int i = 0; i < num_attrs; i++) {
        if (!cbor_value_is_unsigned_integer(&mapIter)) {
            return OT_COAP_CODE_NOT_ACCEPTABLE;
        }
        int label;
        err = cbor_value_get_int_checked(&mapIter, &label);
        if (err != CborNoError) {                        
            return OT_COAP_CODE_NOT_ACCEPTABLE;
        }
        err = cbor_value_advance(&mapIter);
        if (err != CborNoError) {                        
            return OT_COAP_CODE_NOT_ACCEPTABLE;
        }
        switch (aspect->type) {
            case DEVICE_ASPECT_TYPE_LIGHT:
                err = setLightAspectAttribute(&aspect->light, label, &mapIter);
                break;
            default:
                return OT_COAP_CODE_NOT_IMPLEMENTED;
        }
        
        if (err != CborNoError) {                        
            return OT_COAP_CODE_NOT_ACCEPTABLE;
        }
        err = cbor_value_advance(&mapIter);
        if (err != CborNoError) {                        
            ESP_LOGW(TAG, "Got Error %d advancing", err);
            return OT_COAP_CODE_NOT_ACCEPTABLE;
        }

    }
    err = cbor_value_leave_container(&val, &mapIter);
    if (err != CborNoError) {                        
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }

    return OT_COAP_CODE_CHANGED;
}

static void lightAspectSetIsOn(light_aspect_t *aspect, bool val) {
    aspect->is_on = val;
    // TODO send notifications
}

static otCoapCode process_light_service_call(device_t *device, light_aspect_t *aspect, light_serviceid_t serviceId, CborValue *params, int numParams) {
    CborError err;

    switch (serviceId) {
        case LIGHT_SERVICEID_TOGGLE:
            if (numParams != 0) {
                ESP_LOGW(TAG, "Toggle has no parameters");
                return OT_COAP_CODE_NOT_ACCEPTABLE;
            }
            ESP_LOGI(TAG, "Toggling is_on");
            if (aspect->is_on) {
                dali_send_command(DALI_GEAR_ADDR(1) | DALI_CMD_OFF, 0);
                lightAspectSetIsOn(aspect, false);
            } else {
                dali_send_command(DALI_GEAR_ADDR(1) | DALI_CMD_GOTO_LAST_ACTIVE_LEVEL, 0);
                lightAspectSetIsOn(aspect, true);
            }
            return OT_COAP_CODE_CHANGED;
            break;
        case LIGHT_SERVICEID_APPLY_BRIGHTNESS_DELTA:
            if (numParams != 1) {
                ESP_LOGW(TAG, "apply_brightness_delta takes exactly one argument");
                return OT_COAP_CODE_NOT_ACCEPTABLE;
            }
            if (!cbor_value_is_integer(params)) {
                ESP_LOGW(TAG, "Param must be an integer");
                return OT_COAP_CODE_NOT_ACCEPTABLE;
            }
            int delta;
            err = cbor_value_get_int_checked(params, &delta);
            if (err != CborNoError) {                        
                ESP_LOGW(TAG, "Error parsing delta");
                return OT_COAP_CODE_NOT_ACCEPTABLE;
            }
            err = cbor_value_advance(params);
            if (err != CborNoError) {           
                ESP_LOGW(TAG, "Error getting params %d", err);             
                return OT_COAP_CODE_NOT_ACCEPTABLE;
            }

            ESP_LOGI(TAG, "Applying delta of %d", delta);

            // Parameters parsed successfully.  Proceed to make the change. 
            if (!aspect->is_on) {
                dali_send_command(DALI_GEAR_ADDR(1) | DALI_CMD_GOTO_LAST_ACTIVE_LEVEL, 0);
                lightAspectSetIsOn(aspect, false);
            }
            if (delta > 0) {
                dali_send_command(DALI_GEAR_ADDR(1) | DALI_CMD_UP, 0);
            } else if (delta < 0) {
                dali_send_command(DALI_GEAR_ADDR(1) | DALI_CMD_DOWN, 0);
            } else {
                // Delta is zero, which is silly
                ESP_LOGW(TAG, "Attempt to apply delta of 0");             
                return OT_COAP_CODE_NOT_ACCEPTABLE;
            }
            int res = dali_send_command(DALI_GEAR_ADDR(1) | DALI_CMD_QUERY_ACTUAL_LEVEL, pdMS_TO_TICKS(200));
            if (res > 0) {
                ESP_LOGI(TAG, "Level is now %d", res);
                aspect -> level = res;
                // TODO notify listeners of new level
            } else {
                ESP_LOGW(TAG, "Error fetching level after delta");
            }
            return OT_COAP_CODE_CHANGED;


        default:
            ESP_LOGW(TAG, "ServiceID  %d not implemented", serviceId);
            return OT_COAP_CODE_NOT_IMPLEMENTED;
    }

    return OT_COAP_CODE_CHANGED;
}


static otCoapCode process_service_call(device_t *device, device_aspect_t *aspect, uint8_t *buf, size_t len) {

    // Read in the body of the request
    CborParser parser;
    otCoapCode callResult;
    CborValue val, listIter;
    CborError err = cbor_parser_init(buf, len, 0, &parser, &val);
    len = 0;

    ESP_LOGI(TAG, "Processing service call");

    if (err != CborNoError) {                        
        ESP_LOGW(TAG, "Error creating parser %d", err);
        return OT_COAP_CODE_INTERNAL_ERROR;
    }
    if (!cbor_value_is_array(&val)) {
        ESP_LOGW(TAG, "Expected Array input %d", val.type);
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    size_t item_count;
    err = cbor_value_get_array_length(&val, &item_count);
    if (err != CborNoError) {         
        ESP_LOGW(TAG, "Could not get array length");               
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    if (item_count < 1) {
        ESP_LOGW(TAG, "Service calls must have at least one element");
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }

    err = cbor_value_enter_container(&val, &listIter);
    if (err != CborNoError) {      
        ESP_LOGW(TAG, "Could not enter array");         
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    if (!cbor_value_is_unsigned_integer(&listIter)) {
        ESP_LOGW(TAG, "Service parameter is not an integer");
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }
    int serviceId;
    err = cbor_value_get_int_checked(&listIter, &serviceId);
    if (err != CborNoError) {                        
        ESP_LOGW(TAG, "Could not fetch serviceId");
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }


    err = cbor_value_advance(&listIter);
    if (err != CborNoError) {           
        ESP_LOGI(TAG, "Error getting params %d", err);             
        return OT_COAP_CODE_NOT_ACCEPTABLE;
    }

    // Make the service call. 
    switch (aspect->type) {
        case DEVICE_ASPECT_TYPE_LIGHT:
            callResult = process_light_service_call(device, &aspect->light, serviceId, &listIter, item_count-1);
            break;
        default:
            ESP_LOGW(TAG, "Aspect type %d not supported", aspect->type);
            return OT_COAP_CODE_NOT_IMPLEMENTED;
    }
    // err = cbor_value_leave_container(&val, &listIter);
    // if (err != CborNoError) {         
    //     ESP_LOGW(TAG, "Couldn't exit container");               
    //     return OT_COAP_CODE_NOT_ACCEPTABLE;
    // }

    // TODO make it return data, if any.
    return callResult;
}





static void handle_coap_request(void *aContext, otMessage *request_message, const otMessageInfo *aMessageInfo) {
    otCoapOptionIterator iter;
    uint8_t buf[2048];
    size_t len = 0;
    otMessage *response = NULL;
    otError error = OT_ERROR_NONE;
    otCoapCode requestCode;
    otCoapCode responseCode = OT_COAP_CODE_NOT_FOUND;
    uint8_t *bufPtr = buf;
    uint8_t *pathElem[3];
    size_t pathLen[3];
    int numPath = 0;

    ESP_LOGI(TAG, "Handling request");

    otCoapOptionIteratorInit(&iter, request_message);
    requestCode = otCoapMessageGetCode(request_message);

    const otCoapOption *opt = otCoapOptionIteratorGetFirstOption(&iter);
    while (opt) {
        switch (opt->mNumber) {
            case OT_COAP_OPTION_URI_PATH:
                assert(otCoapOptionIteratorGetOptionValue(&iter, bufPtr) == OT_ERROR_NONE);
                pathElem[numPath] = bufPtr;
                pathLen[numPath++] = opt->mLength;
                bufPtr += opt->mLength;
                *bufPtr++ = 0;
                break;
            case OT_COAP_OPTION_URI_QUERY:
                assert(otCoapOptionIteratorGetOptionValue(&iter, bufPtr) == OT_ERROR_NONE);
                ESP_LOGI(TAG, "Query %.*s", opt->mLength, bufPtr);
                break;
            default:
                ESP_LOGI(TAG, "Ignoring option %d", opt->mNumber);
        }
        opt = otCoapOptionIteratorGetNextOption(&iter);
    }

    switch (numPath) {
        case 3:
            // Respond to d/{deviceId}/${aspect}
            if (strcmp((char *) pathElem[0], "d") != 0) {
                responseCode = OT_COAP_CODE_NOT_FOUND;
                break;
            }

            device_t *dev = findDevice(pathElem[1], pathLen[1]);
            if (dev == NULL) {
                responseCode = OT_COAP_CODE_NOT_FOUND;
                break;
            }
            if (pathLen[2] != 1) {
                responseCode = OT_COAP_CODE_NOT_FOUND;
                break;
            }
            device_aspect_t *aspect = getAspect(dev, pathElem[2][0]);
            if (aspect == NULL) {
                responseCode = OT_COAP_CODE_NOT_FOUND;
                break;
            }

            switch (requestCode) {
                case OT_COAP_CODE_GET:
                    // Encode all attributes as a CBOR map.
                    len = encodeAspectAttributes(aspect, buf, sizeof(buf));
                    responseCode = OT_COAP_CODE_CONTENT;
                    break;
                case OT_COAP_CODE_PUT:
                    len = otMessageRead(request_message, otMessageGetOffset(request_message), buf, sizeof(buf));
                    responseCode = process_attribute_udpate(dev, aspect, buf, len);
                    len = 0;
                    break;
                case OT_COAP_CODE_POST:
                    len = otMessageRead(request_message, otMessageGetOffset(request_message), buf, sizeof(buf));
                    responseCode = process_service_call(dev, aspect, buf, len);
                    len = 0;
                    break;
                    
                default:
                    break;
            }
            break;
    }

    otInstance *instance = esp_openthread_get_instance();
    response = otCoapNewMessage(instance, NULL);
	if (response == NULL) {
		goto end;
	}

	otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE, responseCode);
	error = otCoapMessageSetToken(response, otCoapMessageGetToken(request_message), otCoapMessageGetTokenLength(request_message));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		goto end;
	}
	error = otMessageAppend(response, buf, len);
	if (error != OT_ERROR_NONE) {
		goto end;
	}
	error = otCoapSendResponse(instance, response, aMessageInfo);
end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}
}


static void ot_task_worker(void *aContext)
{
    esp_openthread_platform_config_t config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {                                              
            .host_connection_mode = HOST_CONNECTION_MODE_CLI_UART,  
            .host_uart_config = {                                   
                .port = 0,                                          
                .uart_config =                                      
                    {                                               
                        .baud_rate = 115200,                     
                        .data_bits = UART_DATA_8_BITS,              
                        .parity = UART_PARITY_DISABLE,              
                        .stop_bits = UART_STOP_BITS_1,              
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,      
                        .rx_flow_ctrl_thresh = 0,                   
                        .source_clk = UART_SCLK_DEFAULT,            
                    },                                              
                .rx_pin = UART_PIN_NO_CHANGE,                       
                .tx_pin = UART_PIN_NO_CHANGE,                       
            },                                                      
        },
        .port_config = {
            .storage_partition_name = "ot_storage",
            .netif_queue_size = 10,        
            .task_queue_size = 10,
        },
    };

    // Initialize the OpenThread stack
    ESP_ERROR_CHECK(esp_openthread_init(&config));

    // The OpenThread log level directly matches ESP log level
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);

    // Initialize the OpenThread cli
    esp_openthread_cli_init();

    esp_netif_t *openthread_netif;
    // Initialize the esp_netif bindings
    openthread_netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(openthread_netif);

    // esp_cli_custom_command_init();

    otInstance *instance = esp_openthread_get_instance();
    otIp6SetAddressCallback(instance, ipAddressChangeCallback, NULL);

    // If we're configured, automatically turn on thread.
    if (otDatasetIsCommissioned(instance)) {
        assert(otIp6SetEnabled(instance, true) == OT_ERROR_NONE);
        assert(otThreadSetEnabled(instance, true) == OT_ERROR_NONE);
    }   
    // Enable responding to echo requests (ping)
    otIcmp6SetEchoMode(instance, OT_ICMP6_ECHO_HANDLER_ALL);

    assert(otCoapStart(instance, OT_DEFAULT_COAP_PORT) == OT_ERROR_NONE);
    otCoapAddResource(instance, &testResource);
    otCoapSetDefaultHandler(instance, handle_coap_request, NULL);
    
    // Run the main loop
    esp_openthread_cli_create_task();
    esp_openthread_launch_mainloop();

    // Clean up
    esp_netif_destroy(openthread_netif);
    esp_openthread_netif_glue_deinit();

    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}


void start_mdns_service()
{
    ESP_LOGI(TAG, "Enabling MDNS");
    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    //set hostname
    ESP_ERROR_CHECK(mdns_hostname_set("testyxxx"));
    //set default instance
    ESP_ERROR_CHECK(mdns_instance_name_set("CCPEED thing"));


    static uint8_t txtBuf[100]; // TODO not re-entrant
    CborEncoder encoder, listEncoder;
    cbor_encoder_init(&encoder, txtBuf, sizeof(txtBuf), 0);
    cbor_encoder_create_array(&encoder, &listEncoder, 2);
    cbor_encode_text_stringz(&listEncoder, "abc123");
    cbor_encode_text_stringz(&listEncoder, "ded456");
    cbor_encoder_close_container(&encoder, &listEncoder);
    *(encoder.data.ptr) = 0; // add a trailing 0.

    mdns_service_add(NULL, "_ccpeed", "_udp", 5683, NULL, 0);
}

static int read_memory(uint32_t addr, unsigned int bank, unsigned int offset, uint8_t *out, size_t num) {
	if (dali_send_command(DALI_CMD_SET_DTR1 | bank, pdMS_TO_TICKS(500)) != DALI_RESPONSE_NAK) {
		return 1;
	};
	if (dali_send_command(DALI_CMD_SET_DTR0 | offset, pdMS_TO_TICKS(500)) != DALI_RESPONSE_NAK) {
		return 1;
	}

	while (num--) {
		int response = dali_send_command(addr | DALI_CMD_READ_MEMORY_LOCATION, pdMS_TO_TICKS(500));
		if (response >= 0) {
			*out++ = response;
		} else {
			ESP_LOGE(TAG, "Error reading memory bank");
			return 2;
		}
	}
	return 0;
}


static inline char hexToChar(unsigned int val) {
    assert(val < 16);
    if (val < 10) {
        return '0' + val;
    } else {
        return 'A' - 10 + val;
    } 
}


static void deviceIdToStr(device_t *dev, char *out) {
    char *ptr = out;
    uint8_t *bufptr = dev->serial.serial;
    for (int i = 0; i < dev->serial.len; i++) {
        *ptr++ = hexToChar((*bufptr) >> 4); 
        *ptr++ = hexToChar((*bufptr++) & 0x0F); 
    }
    *ptr = 0;
}


static int query_dali_device(uint8_t logicalAddr, device_t *dev) {
    uint16_t devAddr = DALI_GEAR_ADDR(logicalAddr);
    char deviceIdStr[41];

    dev->numAspects = 1;
    light_aspect_t *light = &dev->aspects[0].light;
    light->type = DEVICE_ASPECT_TYPE_LIGHT;


	int res = dali_send_command(devAddr | DALI_CMD_QUERY_DEVICE_TYPE, pdMS_TO_TICKS(500));
	if (res < DALI_RESPONSE_NAK) {
		ESP_LOGW(TAG, "Error scanning for device %d", logicalAddr);
		return 1;
	}

	if (res == DALI_RESPONSE_NAK) {
		ESP_LOGD(TAG, "Device %u not found", logicalAddr);
		return 1;
	}
    ESP_LOGI(TAG, "Device %d is of type %d", logicalAddr, res);

	light->lightType = res;
	if ((res = dali_send_command(devAddr | DALI_CMD_QUERY_ACTUAL_LEVEL, pdMS_TO_TICKS(500))) < 0) {
        ESP_LOGW(TAG, "Didn't get response from querying Level of device %d", logicalAddr);
		return 1;
	}
	light->level = res;

	if ((res = dali_send_command(devAddr | DALI_CMD_QUERY_MIN_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return 1;
	}
	light->min_level = res;
	
	if ((res = dali_send_command(devAddr | DALI_CMD_QUERY_MAX_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return 1;
	}
	light->max_level = res;
	if ((res = dali_send_command(devAddr | DALI_CMD_QUERY_POWER_ON_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return 1;
	}
	light->power_on_level = res;

	if ((res = dali_send_command(devAddr | DALI_CMD_QUERY_GROUPS_ZERO_TO_SEVEN, pdMS_TO_TICKS(500))) < 0) {
		return 1;
	}
	light->group_membership = res;
	if ((res = dali_send_command(devAddr | DALI_CMD_QUERY_GROUPS_EIGHT_TO_FIFTEEN, pdMS_TO_TICKS(500))) < 0) {
		return 1;
	}
	light->group_membership |= res << 8;

    // DALI unique identifier is a concatenation of its GTIN (6 bytes at offset 3) and its serial (8 bytes at offset 10)
	if ((res = read_memory(devAddr, 0, 3, dev->serial.serial, 6))) {
		ESP_LOGW(TAG, "Error reading memory bank 0: %d", res);
		return 1;
	};
	if ((res = read_memory(devAddr, 0, 10, dev->serial.serial+6, 8))) {
		ESP_LOGW(TAG, "Error reading memory bank 0: %d", res);
		return 1;
	};
    dev->serial.len = 14;

    deviceIdToStr(dev, deviceIdStr);
    ESP_LOGI(TAG, "Found device %s at addr %d type %d, level %d range(%d,%d) groups 0x%x", deviceIdStr, logicalAddr, light->lightType, light->level, light->min_level, light->max_level, light->group_membership);
	return 0;
}


static void scanDaliBus() {
    device_t *dev = devices;

    num_devices = 0;
    ESP_LOGI(TAG, "Scanning for DALI Devices");
    for (int i = 0; i < 64; i++) {
        if (query_dali_device(i, dev) == 0) {
            dev++;
            num_devices++;
        }
    }
    ESP_LOGI(TAG, "Finished scan");
}

void app_main(void)
{

    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 5,
    };


    ESP_ERROR_CHECK(nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    xTaskCreate(ot_task_worker, "ot_cli_main", 10240, xTaskGetCurrentTaskHandle(), 5, NULL);

    start_mdns_service();

    dali_init();
    scanDaliBus();
    // dali_send_command(DALI_GEAR_ADDR(1) | DALI_CMD_OFF, 0);

    ESP_LOGI(TAG, "Scanned %d devices", num_devices);
}
