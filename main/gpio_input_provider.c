
#include <stdint.h>
#include "gpio_input_provider.h"
#include "ccpeed_err.h"
#include "device.h"
#include "provider.h"
#include "aspect_button.h"
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include "freertos/queue.h"
#include <esp_log.h>
#include "esp_timer.h"

static int gpio_input_aspects[] = {
    ASPECT_BUTTON_ID
};

static TaskHandle_t button_action_handler_task = NULL;
static QueueHandle_t button_action_queue = NULL;

const static char *TAG = "buttons";

#define DEBOUNCE_USEC (50*1000)
#define LONGPRESS_USEC (750*1000)
#define LONGPRESS_REPEAT_USEC (250*1000)

#define REPEAT_CLICK_TIMEOUT_TICKS (200*1000)

// How long between press and release to count as a click.
#define CLICK_THRESHOLD_TICKS pdMS_TO_TICKS(750)



static bool emit_device_event(gpio_input_device_t *device, button_event_type_t type, TickType_t ts, bool fromISR) {
    button_event_t evt = {
        .clickCount = device->click_count,
        .repeatCount = device->repeat_count,
        .ts = ts,
        .device = &device->super,
        .type = type,
    };

    if (fromISR) {
        BaseType_t taskWoken;
        xQueueSendFromISR(button_action_queue, &evt, &taskWoken);
        return taskWoken == pdTRUE;
    } else {
        return xQueueSend(button_action_queue, &evt, portMAX_DELAY) == pdTRUE;
    }
    
}

static void gpio_input_device_set_state(gpio_input_device_t *self, button_state_t state, TickType_t ts) {
    // Timers only make sense within a state, so stop any outstanding (ignore ones that aren't running)
    esp_timer_stop(self->timer);
    self->state = state;
    self->last_change = ts;

    switch (state) {
        case BTN_STATE_PRESSED_DEBOUNCE:
            // The only way inot pressed_debounce is when we start a press.  Emit that event
            self->last_press_tick = ts;
            self->click_count++;
            self->repeat_count = 0; // For cleanliness sake.
            emit_device_event(self, BTN_EVT_PRESSED, ts, true);
            esp_timer_start_once(self->timer, DEBOUNCE_USEC);
            break;
        case BTN_STATE_RELEASED_DEBOUNCE:
            // The only way inot released_debounce is when we start a release.  Emit that event
            self->last_release_tick = ts;
            self->repeat_count = 0;
            emit_device_event(self, BTN_EVT_RELEASED, ts, true);
            esp_timer_start_once(self->timer, DEBOUNCE_USEC);
            break;

        case BTN_STATE_PRESSED:
            esp_timer_start_once(self->timer, LONGPRESS_USEC);
            break;

        case BTN_STATE_RELEASED:
            // esp_timer_start_once(self->timer, CLICK_ZERO_TIMEOUT_USEC);
            break;

        default:
            // Do nothing
            break;
    }

}

static void gpio_isr(void *arg) {
    gpio_input_provider_t *prov = (gpio_input_provider_t *) arg;
    gpio_input_device_t *device = &prov->device;

    int level = gpio_get_level(prov->pin);
    TickType_t ts = xTaskGetTickCountFromISR();

    switch (device->state) {
        case BTN_STATE_RELEASED_DEBOUNCE:
        case BTN_STATE_PRESSED_DEBOUNCE:
            // We are ignoring everything during debounce.  We exit this state via timer.
            break;

        case BTN_STATE_RELEASED:
            if (level == 0) {
                // Its been pressed.
                gpio_input_device_set_state(device, BTN_STATE_PRESSED_DEBOUNCE, ts);
            } else {
                ESP_EARLY_LOGD(TAG, "Ignoring level 1 while in RELEASED state");
            }
            break;
        case BTN_STATE_PRESSED:
            if (level == 1) {
                // Its been released
                gpio_input_device_set_state(device,  BTN_STATE_RELEASED_DEBOUNCE, ts);
            } else {
                ESP_EARLY_LOGD(TAG, "Ignoring level 0 while in PRESSED state");
            }
            break;
        default:
            ESP_EARLY_LOGW(TAG, "Edge detected while in bad state %d", device->state);
            break;

    }
}

static void timer_expired(void *args) {
    gpio_input_device_t *device = args;
    gpio_input_provider_t *prov = (gpio_input_provider_t *) device->super.provider;

    TickType_t ts = xTaskGetTickCountFromISR();
    int level = gpio_get_level(prov->pin);


    switch (device->state) {
        case BTN_STATE_RELEASED_DEBOUNCE:
            if (level) {
                gpio_input_device_set_state(device, BTN_STATE_RELEASED, ts);
                // If the time between the release and the press are within the click timeout, set a timer
                // to see if we get another press.  If it times out, emit a click event.  If we get another press, then don't
                if (device->last_release_tick - device->last_press_tick <= CLICK_THRESHOLD_TICKS) {
                    esp_timer_start_once(device->timer, REPEAT_CLICK_TIMEOUT_TICKS);
                } else {
                    device->click_count = 0;
                }
            } else {
                ESP_EARLY_LOGW(TAG, "Button pressed at end of release debounce");
                gpio_input_device_set_state(device, BTN_STATE_PRESSED_DEBOUNCE, ts);
            }
            break;
        case BTN_STATE_PRESSED_DEBOUNCE:
            // We are ignoring everything during debounce
            if (level) {
                ESP_EARLY_LOGW(TAG, "Button released at end of press debounce");
                gpio_input_device_set_state(device, BTN_STATE_RELEASED_DEBOUNCE, ts);
            } else {
                gpio_input_device_set_state(device, BTN_STATE_PRESSED, ts);
            }
            break;

        case BTN_STATE_RELEASED:
            // Nobody re-pressed within the click duration, so we emit an event with the click count
            if (device->repeat_count == 0) {
                emit_device_event(device, BTN_EVT_CLICK, ts, true);
            } 
            device->click_count = 0;
            break;

        case BTN_STATE_PRESSED:
            emit_device_event(device, device->repeat_count++ ? BTN_EVT_LONG_PRESS_REPEAT : BTN_EVT_LONG_PRESS, ts, true);
            esp_timer_start_once(device->timer, LONGPRESS_REPEAT_USEC);
            break;

        default:
            ESP_EARLY_LOGW(TAG, "Timer expired in bad state %d", device->state);
            break;
    }

}


static const char *event_names[] = {
    "pressed",
    "released",
    "long press",
    "repeat",
    "click",
};

static inline const char *get_event_type_name(button_event_type_t type) {
    if (type < (sizeof(event_names)/sizeof(event_names[0]))) {
        return event_names[type];
    } else {
        return "invalid";
    }
}

void button_action_handler(void *args) {
    button_event_t evt;
    char buf[64];

    while (true) {
        if (xQueueReceive(button_action_queue, &evt, portMAX_DELAY) == pdTRUE) {
            gpio_input_device_t *device = (gpio_input_device_t *) evt.device;

            ESP_LOGI(TAG, "%s %s (count %d, repeat %d), at %lu", get_event_type_name(evt.type), device_identifier_to_str(&device->super.id, buf, sizeof(buf)), evt.clickCount, evt.repeatCount, evt.ts);
        }
    }
    vTaskDelete(NULL);
}


void gpio_input_provider_init(gpio_input_provider_t *self, uint8_t *mac, uint32_t pin) {
    provider_init(&self->super, GPIO_INPUT_PROVIDER_ID);
    self->pin = pin;

    gpio_config_t gpioConfig = {
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = 1 << self->pin,
    };
    if (button_action_handler_task == NULL) {
        gpio_install_isr_service(0);
    }
    ESP_ERROR_CHECK(gpio_config(&gpioConfig));
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, gpio_isr, self));

    gpio_input_device_init(&self->device, self, mac, pin);
    add_device((device_t *) &self->device);

    if (button_action_handler_task == NULL) {
        button_action_queue = xQueueCreate(10, sizeof(button_event_t));
        assert(xTaskCreate(button_action_handler, "gpio_input_handler", 2048, NULL, 5, &button_action_handler_task) == pdTRUE);
    }
}


void gpio_input_device_init(gpio_input_device_t *self, gpio_input_provider_t *prov, uint8_t *mac, uint32_t pin) {
    ESP_LOGI(TAG, "Setting up GPIO input at pin %lu", pin);
    device_identifier_t id;
    id.num_parts = 2;
    id.parts[0].type = DEVID_TYPE_MAC;
    memcpy(id.parts[0].data, mac, 8);
    id.parts[0].len = 8;
    id.parts[1].type = DEVID_TYPE_GPIOPIN;
    id.parts[1].len = 1;
    id.parts[1].data[0] = pin;

    device_init(&self->super, &prov->super, &id, gpio_input_aspects, 1);
    self->last_change = xTaskGetTickCount();
    self->last_press_tick = 0;
    self->last_release_tick = 0;

    esp_timer_create_args_t timer_args = {
        .callback = timer_expired,
        .arg = self,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "Button timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &self->timer));


    if (gpio_get_level(pin)) {
        self->state = BTN_STATE_RELEASED;
        self->click_count = 0;
    } else {
        self->state = BTN_STATE_PRESSED;
        // TODO start timer for repeat etc...
        self->click_count = 1;
    }
}