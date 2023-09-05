#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "provider.h"
#include <cbor.h>
#include "freertos/FreeRTOS.h"
#include "dali_rmt_encoder.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ccpeed_err.h"
#include "device.h"

#define DALI_PROVIDER_ID 2

// Device Addressed Commands (all have bit 8 set)
#define DALI_CMD_OFF 0x100
#define DALI_CMD_UP 0x101
#define DALI_CMD_DOWN 0x102
#define DALI_CMD_STEP_UP 0x103
#define DALI_CMD_STEP_DOWN 0x104
#define DALI_CMD_RECALL_MAX_LEVEL 0x105
#define DALI_CMD_RECALL_MIN_LEVEL 0x106
#define DALI_CMD_STEP_DOWN_AND_OFF 0x107
#define DALI_CMD_ON_AND_STEP_UP 0x108
#define DALI_CMD_ENABLE_DAPC_SEQUENCE 0x109
#define DALI_CMD_GOTO_LAST_ACTIVE_LEVEL 0x10a
#define DALI_CMD_CONTINUOUS_UP 0x10b
#define DALI_CMD_CONTINUOUS_DOWN 0x10c
#define DALI_CMD_GOTO_SCENE_BASE 0x110
#define DALI_CMD_RESET 0x120
#define DALI_CMD_STORE_ACTUAL_LEVEL_IN_DTR0 0x121
#define DALI_CMD_SAVE_PERSISTENT_VARIABLES 0x122
#define DALI_CMD_SET_OPERATING_MODE 0x123
#define DALI_CMD_RESET_MEMORY_BANK 0x124
#define DALI_CMD_IDENTIFY_DEVICE 0x125
#define DALI_CMD_SET_MAX_LEVEL 0x12a
#define DALI_CMD_SET_MIN_LEVEL 0x12b
#define DALI_CMD_SET_SYSTEM_FAILURE_LEVEL 0x12c
#define DALI_CMD_SET_POWER_ON_LEVEL 0x12d
#define DALI_CMD_SET_FADE_TIME 0x12e
#define DALI_CMD_SET_FADE_RATE 0x12f
#define DALI_CMD_SET_EXTENDED_FADE_RATE 0x130
#define DALI_CMD_SET_SCENE_BASE 0x140
#define DALI_CMD_REMOVE_FROM_SCENE_BASE 0x150
#define DALI_CMD_ADD_TO_GROUP_BASE 0x160
#define DALI_CMD_REMOVE_TO_GROUP_BASE 0x170
#define DALI_CMD_SET_SHORT_ADDRESS 0x180
#define DALI_CMD_ENABLE_WRITE_MEMORY 0x181
#define DALI_CMD_QUERY_STATUS 0x190
#define DALI_CMD_QUERY_CONTROL_GEAR_PRESENT 0x191
#define DALI_CMD_QUERY_LAMP_FAILURE 0x192
#define DALI_CMD_QUERY_LAMP_POWER_ON 0x193
#define DALI_CMD_QUERY_LIMIT_ERROR 0x1944
#define DALI_CMD_QUERY_RESET_STATE 0x195
#define DALI_CMD_QUERY_MISSING_SHORT_ADDRESS 0x196
#define DALI_CMD_QUERY_VERSION_NUMBER 0x197
#define DALI_CMD_QUERY_CONTENT_DTR0 0x198
#define DALI_CMD_QUERY_DEVICE_TYPE  0x199
#define DALI_CMD_QUERY_PHYSICAL_MINIMUM  0x19a
#define DALI_CMD_QUERY_POWER_FAILURE  0x19b
#define DALI_CMD_QUERY_CONTENT_DTR1  0x19c
#define DALI_CMD_QUERY_CONTENT_DTR2  0x19d
#define DALI_CMD_QUERY_OPERATING_MODE  0x19e
#define DALI_CMD_QUERY_LIGHT_SOURCE_TYPE  0x19f
#define DALI_CMD_QUERY_ACTUAL_LEVEL 0x1a0
#define DALI_CMD_QUERY_MAX_LEVEL 0x1a1
#define DALI_CMD_QUERY_MIN_LEVEL 0x1a2
#define DALI_CMD_QUERY_POWER_ON_LEVEL 0x1a3
#define DALI_CMD_QUERY_SYSTEM_FAILURE_LEVEL 0x14
#define DALI_CMD_QUERY_FADE_TIME_FADE_RATE 0x15
#define DALI_CMD_QUERY_MAUFACTURER_SPECIFIC_MODE 0x16
#define DALI_CMD_QUERY_NEXT_DEVICE_TYPE 0x17
#define DALI_CMD_QUERY_EXTENDED_FADE_TIME 0x18
#define DALI_CMD_QUERY_CONTROL_GEAR_FAILURE 0x1a
#define DALI_CMD_QUERY_SCENE_LEVEL 0x1b0
#define DALI_CMD_QUERY_GROUPS_ZERO_TO_SEVEN 0x1c0
#define DALI_CMD_QUERY_GROUPS_EIGHT_TO_FIFTEEN 0x1c1
#define DALI_CMD_QUERY_RANDOM_ADDRESS_H 0x1c2
#define DALI_CMD_QUERY_RANDOM_ADDRESS_M 0x1c3
#define DALI_CMD_QUERY_RANDOM_ADDRESS_L 0x1c4
#define DALI_CMD_READ_MEMORY_LOCATION 0x1c5

// Special Commands - These are broadcast
#define DALI_CMD_TERMINATE 0xa100
#define DALI_CMD_INITIALISE 0xa500
#define DALI_CMD_RANDOMISE 0xa700
#define DALI_CMD_COMPARE 0xa900
#define DALI_CMD_WITHDRAW 0xab00
#define DALI_CMD_PING 0xad00
#define DALI_CMD_SEARCH_ADDR_H 0xb100
#define DALI_CMD_SEARCH_ADDR_M 0xb300
#define DALI_CMD_SEARCH_ADDR_L 0xb500
#define DALI_CMD_PROGRAM_SHORT_ADDR 0xb700
#define DALI_CMD_VERIFY_SHORT_ADDR 0xb900
#define DALI_CMD_QUERY_SHORT_ADDR 0xbb00
#define DALI_CMD_ENABLE_DEVICE_TYPE 0xc100
#define DALI_CMD_SET_DTR0 0xa300
#define DALI_CMD_SET_DTR1 0xc300
#define DALI_CMD_SET_DTR2 0xc500
#define DALI_CMD_WRITE_MEMORY_LOCATION 0xc700
#define DALI_CMD_WRITE_MEMORY_LOCATINO_NO_REPLY 0xc900





#define DALI_GEAR_ADDR(x) ((x) << 9)
#define DALI_GROUP_ADDR(x) (1 << 15 | (x) << 9)


#define DALI_RESPONSE_NAK -1
#define DALI_RESPONSE_COLLISION -2
#define DALI_RESPONSE_TIMEOUT -3
#define DALI_RESPONSE_BUS_BUSY -4
#define DALI_RESPONSE_QUEUED -4
#define DALI_RESPONSE_PROCESSING -5


typedef struct {
    provider_base_t super;

    uint32_t tx_pin;
    uint32_t rx_pin;

    rmt_channel_handle_t tx_chan;
    rmt_channel_handle_t rx_chan;

    volatile QueueHandle_t command_complete_queue;
    volatile QueueHandle_t pending_cmd_queue;

    rmt_symbol_word_t receiveBuf[64];
    esp_timer_handle_t rxTimeoutTimer;

    TaskHandle_t transcieve_task;
} dali_provider_t;


typedef struct {
    device_t super;
    uint16_t address; // THis is already shifted.

    uint8_t lightType;
    uint16_t group_membership;
    uint8_t level;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t power_on_level;
} dali_device_t;





ccpeed_err_t dali_provider_init(dali_provider_t *self, CborValue *it);
ccpeed_err_t dali_send_command(dali_provider_t *prov, uint16_t value, TickType_t ticksToWait) ;
ccpeed_err_t dali_device_encode_attributes(dali_device_t *_dev, int aspect_id, CborEncoder *encoder);
ccpeed_err_t dali_set_attr(dali_device_t *_dev, int aspect_id, int attr_id, CborValue *val);
ccpeed_err_t dali_process_service_call(dali_device_t *device, int aspectId, int serviceId, CborValue *attr, size_t attr_count);


#ifdef __cplusplus
}
#endif