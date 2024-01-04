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
#include <unistd.h>

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
#include <openthread/udp.h>
#include <openthread/thread.h>
#include <openthread/icmp6.h>
#include <openthread/coap.h>
#include "openthread/tasklet.h"
#include "esp_vfs_eventfd.h"

#include "lua_system.h"
#include "lua_coap.h"



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
            addr = (otNetifAddress *) addr->mNext;
        }


    } else {
        ESP_LOGI(TAG, "IP Address Removed");
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


    otSockAddr sockAddr;
    memset(&sockAddr, 0, sizeof(sockAddr)); // Unspecified (every address)
    sockAddr.mPort = OT_DEFAULT_COAP_PORT;
    otUdpSocket udpSock;

    otError error = otUdpOpen(instance, &udpSock, coapCallback, NULL);
    if (error != OT_ERROR_NONE) {
        ESP_LOGW(TAG, "Error opening UDP");
    } else {
        error = otUdpBind(instance, &udpSock, &sockAddr, OT_NETIF_THREAD);
        if (error != OT_ERROR_NONE) {
            ESP_LOGW(TAG, "Error binding UDP");
        }
    }

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
      .base_path = "/fs",
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
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }



    char *ptr = defaultMacStr;
    for (int i = 0; i < 8; i++) {
        ptr += sprintf(ptr, "%02x", defaultMac[i]);
    }

    if (init_lua() != ESP_OK) {
         ESP_LOGE(TAG, "Error loading LUA interpreter");
    }
    // The Openthread task can clobber logs from the LUA startup.  To combat this, we put a small delay in
    vTaskDelay(pdMS_TO_TICKS(20));


    xTaskCreate(ot_task_worker, "ot_cli_main", 10240, xTaskGetCurrentTaskHandle(), 5, NULL);

}
