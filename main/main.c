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

#include <esp_mac.h>
#include "esp_vfs_eventfd.h"

#include "lua_system.h"
#include "lua_coap.h"

const static char *TAG = "CCPEED Device";


static uint8_t defaultMac[8];
static char defaultMacStr[17];

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

    run_lua_loop();


}
