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
#include "dali_provider.h"
#include "device.h"


#define GPIO_INPUT_PROVIDER_ID 3


typedef enum {
    BTN_STATE_RELEASED,
    BTN_STATE_PRESSED_DEBOUNCE,
    BTN_STATE_PRESSED,
    BTN_STATE_RELEASED_DEBOUNCE,
} button_state_t;

typedef enum {
    BTN_EVT_PRESSED,
    BTN_EVT_RELEASED,
    BTN_EVT_LONG_PRESS,
    BTN_EVT_CLICK,
} button_event_type_t;

typedef struct {
    device_t *device;
    button_event_type_t type;
    TickType_t ts;
    unsigned int clickCount;
    unsigned int repeatCount;
} button_event_t;

typedef struct {
    device_identifier_t target;
    uint32_t targetAspect;
    int32_t targetId; // +ve for attribute, -ve for serviceId.

    uint8_t *expression[64]; // CBOR object. 
} button_action_config_t;

typedef struct {
    device_t super;
    button_state_t state;
    TickType_t last_change;
    TickType_t last_press_tick;
    TickType_t last_release_tick;
    unsigned int click_count;
    unsigned int repeat_count;
    esp_timer_handle_t timer;


} gpio_input_device_t;


typedef struct {
    provider_base_t super;
    uint32_t pin;
    gpio_input_device_t device;
} gpio_input_provider_t;



void gpio_input_provider_init(gpio_input_provider_t *prov, uint8_t *mac, uint32_t pin);


void gpio_input_device_init(gpio_input_device_t *self, gpio_input_provider_t *prov, uint8_t *mac, uint32_t pin);

#ifdef __cplusplus
}
#endif
