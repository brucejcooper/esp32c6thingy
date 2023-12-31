#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <cbor.h>
#include "freertos/FreeRTOS.h"
#include "dali_rmt_encoder.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ccpeed_err.h"

#define DALI_RESPONSE_NAK -1
#define DALI_RESPONSE_COLLISION -2
#define DALI_RESPONSE_TIMEOUT -3
#define DALI_RESPONSE_BUS_BUSY -4
#define DALI_RESPONSE_QUEUED -4
#define DALI_RESPONSE_PROCESSING -5


typedef struct {
    uint32_t tx_pin;
    uint32_t rx_pin;

    rmt_channel_handle_t tx_chan;
    rmt_channel_handle_t rx_chan;

    volatile QueueHandle_t command_complete_queue;
    volatile QueueHandle_t pending_cmd_queue;

    rmt_symbol_word_t receiveBuf[64];
    esp_timer_handle_t rxTimeoutTimer;

    TaskHandle_t transcieve_task;
} dali_driver_t;

typedef void (*dali_command_callback_t)(int result, void *arg);

ccpeed_err_t dali_driver_init(dali_driver_t *driver, uint32_t tx, uint32_t rx);
ccpeed_err_t dali_send_command(dali_driver_t *driver, uint16_t value, dali_command_callback_t cb, void *arg) ;


#ifdef __cplusplus
}
#endif
