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
#include <freertos/task.h>
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <esp_vfs.h>
#include <esp_spiffs.h>

#include "nvs_flash.h"

#include "dali_driver.h"


#include <driver/uart.h>

#include <cbor.h>
#include "cbor_helpers.h"
#include "openthread/cli.h"
#include "openthread/instance.h"
#include "esp_openthread.h"
#include <esp_openthread_cli.h>
#include <esp_mac.h>
#include "esp_openthread_types.h"
#include "esp_openthread_netif_glue.h"
#include "openthread/logging.h"
#include <openthread/platform/settings.h>
#include <openthread/ip6.h>
#include <openthread/dataset.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>
#include <openthread/icmp6.h>
#include <openthread/coap.h>
#include "openthread/tasklet.h"
#include "esp_vfs_eventfd.h"
// #include "ast.h"

#include "lua_system.h"



static uint8_t defaultMac[8];
static char defaultMacStr[17];


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

const static char *TAG = "CCPEED Device";


static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));

    return netif;
}


void srp_server_callback(const otSockAddr *aServerSockAddr, void *aContext) {
    char buf[40];
    otIp6AddressToString(&aServerSockAddr->mAddress, buf, sizeof(buf));
    ESP_LOGI(TAG, "Found server %s:%d", buf, aServerSockAddr->mPort);
}


void register_srp(otNetifAddress *addr)
{
    ESP_LOGI(TAG, "Enabling SRP Service registration");
    otInstance *instance = esp_openthread_get_instance();

    otSrpClientSetHostName(instance, defaultMacStr);
    otSrpClientSetHostAddresses(instance, &addr->mAddress, 1);


    static otSrpClientService service = {
        .mName = "_ccpeed._udp",    ///< The service labels (e.g., "_mt._udp", not the full domain name).
        .mInstanceName = defaultMacStr, ///< The service instance name label (not the full name).
        .mSubTypeLabels = NULL,     ///< Array of sub-type labels (must end with `NULL` or can be `NULL`).
        .mPort = OT_DEFAULT_COAP_PORT,
        .mPriority = 1,             ///< The service priority.
        .mWeight = 1,               ///< The service weight.
        .mNumTxtEntries = 0,        ///< Number of entries in the `mTxtEntries` array.
        .mTxtEntries = NULL,        ///< Array of TXT entries (`mNumTxtEntries` gives num of entries).
        .mLease = 0,                ///< Desired lease interval in sec - zero to use default.
        .mKeyLease = 0,             ///< Desired key lease interval in sec - zero to use default.
    };
    otSrpClientAddService(instance, &service);
    otSrpClientEnableAutoStartMode(instance, srp_server_callback, NULL);
    // otSrpClientEnableAutoStartMode(true);
    // //initialize mDNS service
    // esp_err_t err = mdns_init();
    // if (err) {
    //     printf("MDNS Init failed: %d\n", err);
    //     return;
    // }

    // //set hostname
    // ESP_ERROR_CHECK(mdns_hostname_set("testyxxx"));
    // //set default instance
    // ESP_ERROR_CHECK(mdns_instance_name_set("CCPEED thing"));


    // static uint8_t txtBuf[100]; // TODO not re-entrant
    // CborEncoder encoder, listEncoder;
    // cbor_encoder_init(&encoder, txtBuf, sizeof(txtBuf), 0);
    // cbor_encoder_create_array(&encoder, &listEncoder, 2);
    // cbor_encode_text_stringz(&listEncoder, "abc123");
    // cbor_encode_text_stringz(&listEncoder, "ded456");
    // cbor_encoder_close_container(&encoder, &listEncoder);
    // *(encoder.data.ptr) = 0; // add a trailing 0.

    // mdns_service_add(NULL, "_ccpeed", "_udp", 5683, NULL, 0);
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
                if (!otSrpClientIsRunning(instance)) {
                    register_srp(addr);
                }
            }
            addr = addr->mNext;
        }


    } else {
        ESP_LOGI(TAG, "IP Address Removed");
    }
}

// static void list_devices_handler(void *aContext, otMessage *request_message, const otMessageInfo *aMessageInfo) {
//     ESP_LOGD(TAG, "List devices called");
//     otMessage *response = NULL;
//     otError error = OT_ERROR_NONE;
//     CborEncoder encoder, deviceMapEncoder, deviceEncoder, aspectEncoder;
//     uint8_t buf[2000];
//     uint8_t buf2[100];
//     size_t sz2;
//     CborError cerr;

//     otInstance *instance = esp_openthread_get_instance();

//     response = otCoapNewMessage(instance, NULL);
// 	if (response == NULL) {
// 		goto end;
// 	}
// 	error = otCoapMessageInitResponse(response, request_message, OT_COAP_TYPE_ACKNOWLEDGMENT, OT_COAP_CODE_CONTENT);
// 	if (error != OT_ERROR_NONE) {
// 		goto end;
// 	}

// 	error = otCoapMessageSetPayloadMarker(response);
// 	if (error != OT_ERROR_NONE) {
// 		goto end;
// 	}

//     cbor_encoder_init(&encoder, buf, sizeof(buf), 0);
//     // TODO race condition. Lock it for the duration.
//     cbor_encoder_create_map(&encoder, &deviceMapEncoder, device_count());
//     for (device_t *dev = device_get_all(); dev != NULL; dev = (device_t *) dev->_llitem.next) {
//         // Key
//         sz2 = sizeof(buf2);
//         cerr = cbor_encode_deviceid(&dev->id, buf2, &sz2);
//         if (cerr != CborNoError) {
//             ESP_LOGE(TAG, "Error encoding deviceID: %d", cerr);
//             goto end;
//         }
//         cbor_encode_byte_string(&deviceMapEncoder, buf2, sz2);
//         // Value
//         cbor_encoder_create_array(&deviceMapEncoder, &deviceEncoder, 1);
//             // Only field - the array of aspects
//             cbor_encoder_create_array(&deviceEncoder, &aspectEncoder, dev->num_aspects);
//             for (int i = 0; i < dev->num_aspects; i++) {
//                 cbor_encode_uint(&aspectEncoder, dev->aspects[i]);
//             }
//             cbor_encoder_close_container(&deviceEncoder, &aspectEncoder);
//         cbor_encoder_close_container(&deviceMapEncoder, &deviceEncoder);
//     }
//     cbor_encoder_close_container(&encoder, &deviceMapEncoder);

// 	error = otMessageAppend(response, buf, encoder.data.ptr-buf);
// 	if (error != OT_ERROR_NONE) {
// 		goto end;
// 	}

// 	error = otCoapSendResponse(instance, response, aMessageInfo);
// 	ESP_LOG_BUFFER_HEXDUMP(TAG, buf, encoder.data.ptr-buf, ESP_LOG_DEBUG);

// end:
// 	if (error != OT_ERROR_NONE && response != NULL) {
// 		otMessageFree(response);
// 	}

// }


// static otCoapCode process_attribute_udpate(device_t *device, int aspectId, uint8_t *buf, size_t len) {

//     // Read in the body of the request
//     CborParser parser;
//     CborValue val;
//     CborError err = cbor_parser_init(buf, len, 0, &parser, &val);
//     ccpeed_err_t cerr;
//     len = 0;
//     if (err != CborNoError) {                        
//         ESP_LOGW(TAG, "Error creating parser %d", err);
//         return OT_COAP_CODE_NOT_ACCEPTABLE;
//     }
//     cerr = device->provider->set_attr_fn(device, aspectId, &val);
//     if (cerr != CCPEED_NO_ERR) {                        
//         return cerr;
//     }

//     return OT_COAP_CODE_CHANGED;
// }


// static otCoapCode process_service_call(device_t *device, int aspectId, uint8_t *buf, size_t len) {

//     // Read in the body of the request
//     CborParser parser;
//     otCoapCode callResult = OT_COAP_CODE_INTERNAL_ERROR;
//     CborValue val, listIter;
//     CborError err = cbor_parser_init(buf, len, 0, &parser, &val);
//     ccpeed_err_t cerr;
//     len = 0;

//     ESP_LOGD(TAG, "Processing service call");

//     if (err != CborNoError) {                        
//         ESP_LOGW(TAG, "Error creating parser %d", err);
//         return OT_COAP_CODE_INTERNAL_ERROR;
//     }
//     if (!cbor_value_is_array(&val)) {
//         ESP_LOGW(TAG, "Expected Array input %d", val.type);
//         return OT_COAP_CODE_NOT_ACCEPTABLE;
//     }
//     size_t item_count;
//     err = cbor_value_get_array_length(&val, &item_count);
//     if (err != CborNoError) {         
//         ESP_LOGW(TAG, "Could not get array length");               
//         return OT_COAP_CODE_NOT_ACCEPTABLE;
//     }
//     if (item_count < 1) {
//         ESP_LOGW(TAG, "Service calls must have at least one element");
//         return OT_COAP_CODE_NOT_ACCEPTABLE;
//     }

//     err = cbor_value_enter_container(&val, &listIter);
//     if (err != CborNoError) {      
//         ESP_LOGW(TAG, "Could not enter array");         
//         return OT_COAP_CODE_NOT_ACCEPTABLE;
//     }
//     uint32_t serviceId;
//     err = cbor_expect_uint32(&listIter, UINT32_MAX, &serviceId);
//     if (err != CborNoError) {                        
//         ESP_LOGW(TAG, "Could not fetch serviceId");
//         return OT_COAP_CODE_NOT_ACCEPTABLE;
//     }


//     err = cbor_value_advance(&listIter);
//     if (err != CborNoError) {           
//         ESP_LOGI(TAG, "Error getting params %d", err);             
//         return OT_COAP_CODE_NOT_ACCEPTABLE;
//     }

//     if (device->provider->process_service_call_fn) {
//         cerr = device->provider->process_service_call_fn(device, aspectId, serviceId, &listIter, item_count-1);
//         switch (cerr) {
//             case CCPEED_NO_ERR:
//                 callResult = OT_COAP_CODE_CHANGED;
//                 break;
//             case CCPEED_ERROR_NOT_FOUND:
//                 callResult = OT_COAP_CODE_NOT_FOUND;
//                 break;
//             case CCPEED_ERROR_NOT_IMPLEMENTED:
//                 callResult = OT_COAP_CODE_NOT_IMPLEMENTED;
//                 break;
//             default:
//                 ESP_LOGE(TAG, "Unknown return code %d", cerr);
//                 callResult = OT_COAP_CODE_INTERNAL_ERROR;
//                 break;
//         }
//     }
//     return callResult;
// }


// static const otMessageSettings msgSettings = {
//     .mLinkSecurityEnabled = false,
//     .mPriority = OT_MESSAGE_PRIORITY_NORMAL
// };


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
    CborEncoder encoder;
    uint64_t intOpt;
    ccpeed_err_t err;
    int numPath = 0;
    otInstance *instance = esp_openthread_get_instance();
    int32_t observeRequested = -1;

    // otMessage *noti = otCoapNewMessage(instance, &msgSettings);
    // otCoapMessageSetCode(noti, OT_COAP_CODE_CONTENT);

    ESP_LOGD(TAG, "Handling request");

    otCoapOptionIteratorInit(&iter, request_message);
    requestCode = otCoapMessageGetCode(request_message);

    uint8_t tokenSz = otCoapMessageGetTokenLength(request_message);
    const uint8_t *token = otCoapMessageGetToken(request_message);

    const otCoapOption *opt = otCoapOptionIteratorGetFirstOption(&iter);
    while (opt) {
        switch (opt->mNumber) {
            case OT_COAP_OPTION_URI_PATH:
                assert(otCoapOptionIteratorGetOptionValue(&iter, bufPtr) == OT_ERROR_NONE);
                pathElem[numPath] = bufPtr;
                pathLen[numPath++] = opt->mLength;
                ESP_LOGI(TAG, "Got path option %.*s",  opt->mLength, (char *) bufPtr);
                bufPtr += opt->mLength;
                *bufPtr++ = 0;
                break;
            case OT_COAP_OPTION_URI_QUERY:
                assert(otCoapOptionIteratorGetOptionValue(&iter, bufPtr) == OT_ERROR_NONE);
                ESP_LOGI(TAG, "Query %.*s", opt->mLength, bufPtr);
                break;
            case OT_COAP_OPTION_OBSERVE:
                assert(otCoapOptionIteratorGetOptionUintValue(&iter, &intOpt) == OT_ERROR_NONE);
                ESP_LOGI(TAG, "COAP Observe option set to %llu", intOpt);
                observeRequested = intOpt;

                break;
            default:
                ESP_LOGI(TAG, "Ignoring option %d", opt->mNumber);
        }
        opt = otCoapOptionIteratorGetNextOption(&iter);
    }


    ESP_LOGD(TAG, "Number of path elements is %d", numPath);
    // switch (numPath) {
    //     case 3:
    //         // Respond to d/{deviceId}/${aspect}
    //         if (strcmp((char *) pathElem[0], "d") != 0) {
    //             ESP_LOGI(TAG, "First path element is not d");
    //             responseCode = OT_COAP_CODE_NOT_FOUND;
    //             break;
    //         }

    // 	    err = deviceid_decode(&identifier, pathElem[1], pathLen[1]);
    //         if (err != CCPEED_NO_ERR) {
    //             ESP_LOGW(TAG, "Couldn't parse deviceID");
    //             ESP_LOG_BUFFER_HEXDUMP(TAG, pathElem[1], pathLen[1], ESP_LOG_WARN);

    //             responseCode = OT_COAP_CODE_NOT_ACCEPTABLE;
    //             break;
    //         }

    //         device_t *dev = device_find_by_id(&identifier);
    //         if (dev == NULL) {
    //             ESP_LOGW(TAG, "Couldn't find device %s", device_identifier_to_str(&identifier, (char *) buf, sizeof(buf)));
    //             responseCode = OT_COAP_CODE_NOT_FOUND;
    //             break;
    //         }
    //         if (pathLen[2] != 1) {
    //             ESP_LOGW(TAG, "Extension too long");
    //             responseCode = OT_COAP_CODE_NOT_FOUND;
    //             break;
    //         }
    //         int aspectId = pathElem[2][0];

    //         if (!device_has_aspect(dev, aspectId)) {
    //             ESP_LOGW(TAG, "Device does not have aspect %d", aspectId);
    //             responseCode = OT_COAP_CODE_NOT_FOUND;
    //             break;
    //         }



    //         switch (requestCode) {
    //             case OT_COAP_CODE_GET:
    //                 ESP_LOGD(TAG, "Request is GET");

    //                 // Append the subscription, if requested.
    //                 if (observeRequested != -1) {
    //                     // See if there is an existing one. 
    //                     ESP_LOGI(TAG, "Searching for existing subscription");
    //                     subscription_t *sub = subscription_find(&identifier, aspectId, token, tokenSz, aMessageInfo);
    //                     if (sub) {
    //                         ESP_LOGI(TAG, "Removing existing subscription");
    //                         err = subscription_delete(sub);
    //                         if (err != CCPEED_NO_ERR) {
    //                             responseCode = OT_COAP_CODE_INTERNAL_ERROR;
    //                             break;
    //                         }
    //                     } else {
    //                         ESP_LOGI(TAG, "No existing sub to remove");
    //                     }
    //                     if (observeRequested == 0) {
    //                         ESP_LOGI(TAG, "Appending Subscription");
    //                         err = subscription_append(&identifier, aspectId, token, tokenSz, aMessageInfo, 0);
    //                         if (err != CCPEED_NO_ERR) {
    //                             ESP_LOGW(TAG, "Error appending subscription: %d", err);
    //                             responseCode = OT_COAP_CODE_INTERNAL_ERROR;
    //                             break;
    //                         }
    //                     }
    //                 }

    //                 // Encode all attributes as a CBOR map.
    //                 cbor_encoder_init(&encoder, buf, sizeof(buf), 0);
    //                 if (dev->provider->encode_attributes_fn) {
    //                     dev->provider->encode_attributes_fn(dev, aspectId, &encoder);
    //                     len = encoder.data.ptr-buf;
    //                     responseCode = OT_COAP_CODE_CONTENT;
    //                 } else {
    //                     ESP_LOGE(TAG, "Attempt to get attributes of a device that we haven't implemented");
    //                     responseCode = OT_COAP_CODE_NOT_IMPLEMENTED;
    //                 }
    //                 break;
    //             case OT_COAP_CODE_PUT:
    //                 len = otMessageRead(request_message, otMessageGetOffset(request_message), buf, sizeof(buf));
    //                 ESP_LOGD(TAG, "Request is PUT");
    //                 responseCode = process_attribute_udpate(dev, aspectId, buf, len);
    //                 len = 0;
    //                 break;
    //             case OT_COAP_CODE_POST:
    //                 len = otMessageRead(request_message, otMessageGetOffset(request_message), buf, sizeof(buf));
    //                 ESP_LOGD(TAG, "Request is POST");
    //                 responseCode = process_service_call(dev, aspectId, buf, len);
    //                 len = 0;
    //                 break;
                    
    //             default:
    //                 break;
    //         }
    //         break;
    // }

    response = otCoapNewMessage(instance, NULL);
	if (response == NULL) {
		goto end;
	}

  	error = otCoapMessageInitResponse(response, request_message, OT_COAP_TYPE_ACKNOWLEDGMENT, responseCode);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

    if (observeRequested == 0 && requestCode == OT_COAP_CODE_GET && responseCode == OT_COAP_CODE_CONTENT) {
        // Acknowledge the OBSERVE
        otCoapMessageAppendObserveOption(response, observeRequested);
    }

    if (len > 0) {
        error = otCoapMessageSetPayloadMarker(response);
        if (error != OT_ERROR_NONE) {
            goto end;
        }
        ESP_LOGI(TAG, "output");
        ESP_LOG_BUFFER_HEXDUMP(TAG, buf, len, ESP_LOG_INFO);

        error = otMessageAppend(response, buf, len);
        if (error != OT_ERROR_NONE) {
            goto end;
        }
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
            .storage_partition_name = "nvs",
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
    // otCoapAddResource(instance, &testResource);
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


void app_main(void) {
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 5,
    };
    ESP_ERROR_CHECK(nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(defaultMac));

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/lua",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = false
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    ESP_LOGW(TAG, "RET %d", ret);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }



    char *ptr = defaultMacStr;
    for (int i = 0; i < 8; i++) {
        ptr += sprintf(ptr, "%02x", defaultMac[i]);
    }

    xTaskCreate(ot_task_worker, "ot_cli_main", 10240, xTaskGetCurrentTaskHandle(), 5, NULL);

    if (init_lua() != ESP_OK) {
        ESP_LOGE(TAG, "Error loading LUA interpreter");
    }
}
