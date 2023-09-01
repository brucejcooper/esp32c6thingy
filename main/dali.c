

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include <string.h>

#include "esp_log.h"
#include "dali_rmt_encoder.h"
#include "dali.h"


static rmt_encoder_handle_t tx_encoder;

#define TAG "dali"

#define TX_PIN 4
#define RX_PIN 5
#define TX_CHANNEL RMT_CHANNEL_0
#define RX_CHANNEL RMT_CHANNEL_1

#define DALI_BIT_USEC 833
#define DALI_HALF_BIT_USEC (DALI_BIT_USEC/ 2)
// We allow 25% of a half bit tolerance either side for timing. 
#define TIMING_ALLOWANCE 105
#define IS_HALF_BIT(x) (x > (DALI_HALF_BIT_USEC-TIMING_ALLOWANCE) && x < (DALI_HALF_BIT_USEC+TIMING_ALLOWANCE))
#define IS_FULL_BIT(x) (x > (DALI_BIT_USEC-TIMING_ALLOWANCE) && x < (DALI_BIT_USEC+TIMING_ALLOWANCE))


typedef struct {
    uint16_t duration : 15; /*!< Duration of level0 */
    uint16_t level : 1;     /*!< Level of the first part */
} single_pulse_t;

typedef struct {
    uint16_t command;
    SemaphoreHandle_t waiter;
    int *result;
} command_t;


static rmt_channel_handle_t tx_chan = NULL;
static rmt_channel_handle_t rx_chan = NULL;

static bool rx_transaction_done(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx);
static bool tx_transaction_done(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx);
static void dali_worker(void *aContext);

static const rmt_rx_event_callbacks_t rx_callbacks = {
    .on_recv_done = rx_transaction_done
};

static const rmt_tx_event_callbacks_t txcb = {
    .on_trans_done = tx_transaction_done
};


static volatile QueueHandle_t command_complete_queue;
static volatile QueueHandle_t pending_cmd_queue;


static rmt_symbol_word_t receiveBuf[64];
static const rmt_receive_config_t rx_config = {
    .signal_range_min_ns = 2000,
    .signal_range_max_ns = (3*833/2) * 1000,
};

esp_timer_handle_t rxTimeoutTimer;


typedef enum {
    STATE_WAITING_FOR_3RD_PARTY,
    STATE_TRANSMITTING,
    STATE_WAITING_FOR_READBACK,
    STATE_WAITING_FOR_RESPONSE,
    STATE_COLLISION,
    STATE_POST_RESPONSE_DEADTIME,
} dali_state_t;

typedef struct {
    int response; // Maybe negative for certain values (see DALI_RESPONSE_*)
    int numBits;
} rx_command_complete_event_t;

static volatile dali_state_t state = STATE_WAITING_FOR_3RD_PARTY;




int reconstructDaliSignal(const rmt_rx_done_event_data_t *d, uint32_t *result) {
    uint32_t out = 0;
    // We just want a stream of pulses, not pairs, so map it down to array of uint16_t. 
    single_pulse_t *pulse = (single_pulse_t *) d->received_symbols;
    size_t numBits = 0;

    // Its possible there are an odd number of pulses, in which case a 0,0 will be on the end.  Strip it.
    int remaining_pulses = d->num_symbols*2;
    if (pulse[remaining_pulses-1].duration == 0 && pulse[remaining_pulses-1].level == 0) {
        remaining_pulses--;
    }

    // Consume the first pulse, making sure its a half bit that is high (the start marker)
    // ESP_EARLY_LOGI(TAG, "S%d", pulse->duration);
    if (!(pulse->level && IS_HALF_BIT(pulse->duration))) {
        ESP_EARLY_LOGI(TAG, "Invalid start pulse with %d remaining - %d, %d", remaining_pulses, pulse->level, pulse->duration);
        return -1;
    }
    pulse++;
    remaining_pulses--;

    uint16_t lastBit = 1;
    while (remaining_pulses--) {
        // At top of loop we should always be at the half bit.
        uint16_t pd = pulse->duration;
        // ESP_EARLY_LOGI(TAG, "P%d", pulse->duration);


        if (IS_HALF_BIT(pd)) {
            // This puts us at the bit edge.  The only valid next bit is another half bit 
            // or the end of transmission if the current pulse level is high
            if (remaining_pulses) {
                pulse++;
                remaining_pulses--;
                // ESP_EARLY_LOGI(TAG, "E%d", pulse->duration);
                if (!IS_HALF_BIT(pulse->duration)) {
                    return -2;
                }
                // Its the same bit as before
                out = out << 1 | lastBit;
                numBits++;
            } else {
                // We've run out of pulses. This is okay only if this pulse is high (so we are returning to 0)
                if (!pulse->level) {
                    return -3;
                }
                // If we get here, we're done. 
            }
        } else if (IS_FULL_BIT(pd)) {
            // Its going back to the next half bit, but there's a bit flip
            lastBit = !lastBit;
            out = out << 1 | lastBit;
            numBits++;
        } else {
            ESP_EARLY_LOGI(TAG, "Invalid pulse with %d remaining - %d, %d", remaining_pulses, pulse->level, pulse->duration);
            return -4;
        }
        pulse++;
    }

    *result = out;
    return numBits;
}


static void setState(dali_state_t newState, uint64_t timeout_usec) {
    state = newState;
    // Disable any existing timeout
    // Don't check response from this one, as the only possible outcomes are OK or already stopepd. 
    esp_timer_stop(rxTimeoutTimer);
    if (timeout_usec) {
        // Set timeout.
        ESP_ERROR_CHECK(esp_timer_start_once(rxTimeoutTimer, timeout_usec));
    }
}

static bool rx_transaction_done(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_wakeup = pdFALSE;
    rx_command_complete_event_t evt;
    uint32_t data;
    esp_err_t err;
    // immediately stop the read timeout, to avoid race conditions between the timer and the RMT receiver.
    esp_timer_stop(rxTimeoutTimer);

    evt.numBits = reconstructDaliSignal(edata, (uint32_t *) &data);
    // Allow receive to start again (always be receiving)
    err = rmt_receive(rx_chan, receiveBuf, sizeof(receiveBuf), &rx_config);
    if (err != ESP_OK) {
        abort();
    }

    // receiving proceeds in a loop.
    switch (state) {
        case STATE_WAITING_FOR_3RD_PARTY:
            // This is a 3rd party command, wait for the response as well. 
            // No need to send an event.
            setState(STATE_WAITING_FOR_RESPONSE, 20000);
            break;
        case STATE_TRANSMITTING:
            // Got read tx end event while transmitting.  This should never happen
            abort();
            break;
        case STATE_WAITING_FOR_READBACK:
            // This is a readback of something that we just transmitted.
            // TODO confirm it matches what we were sending.  If it does not, there was a collision or other bus error
            // Timer is max time between finishing of the command readback and the finishing of receiving the response with maximum delay between them
            // I'm not 100% sure what this is, but its close to 20ms (probably closer to 17, but leniency is good)
            setState(STATE_WAITING_FOR_RESPONSE, 20000);
            break;
        case STATE_WAITING_FOR_RESPONSE:
            // after receiving a reverse frame, we must wait at least 6 half bit periods (approx 2.5ms) before transmitting again.
            setState(STATE_POST_RESPONSE_DEADTIME, 2500);
            if (evt.numBits == 8) {
                evt.response = (int) data;
                xQueueSendFromISR(command_complete_queue, &evt, &high_task_wakeup);
            } else {
                // Invalid response length.
                ESP_EARLY_LOGI(TAG, "Received %d bits in response, which is wrong!", evt.numBits);
                abort();
            }
            break;
        case STATE_COLLISION:
            // Ignore anything read in collision, until we have some idle time.
            break;
        case STATE_POST_RESPONSE_DEADTIME:
            // If we receive anything in this state, it is a bus violation. 
            ESP_EARLY_LOGE(TAG, "Received something during post-command dead time");
            abort();
    }

    // return whether any task is woken up
    return high_task_wakeup == pdTRUE;
}

static bool tx_transaction_done(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_wakeup = pdFALSE;
    // This only ever happens after transmit of a command has just completed.  This means 
    assert(state == STATE_TRANSMITTING);
    setState(STATE_WAITING_FOR_READBACK, 2200);
    // Set the receive timeout
    // xQueueSendFromISR(rx_wait_queue, &evt, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void rx_timer_expired(void *args) {
    rx_command_complete_event_t evt;
    BaseType_t high_task_wakeup = pdFALSE;

    switch (state) {
        case STATE_WAITING_FOR_3RD_PARTY:
        case STATE_TRANSMITTING:
        case STATE_WAITING_FOR_READBACK:
            // Illegal. TODO consider potential race conditions that could make this happen. 
            abort();
        case STATE_WAITING_FOR_RESPONSE:
            // No response means NAK
            // TODO notify that there's a NAK. 
            evt.numBits = 0;
            evt.response = DALI_RESPONSE_NAK;
            setState(STATE_WAITING_FOR_3RD_PARTY, 0);
            xQueueSendFromISR(command_complete_queue, &evt, &high_task_wakeup);
            break;
        case STATE_COLLISION:
            // Collisions happen while transmitting.  We have cleared the state now, so this means that our transmit failed.  Let
            // the outer loop know. 
            evt.numBits = 0;
            evt.response = DALI_RESPONSE_COLLISION;
            setState(STATE_WAITING_FOR_3RD_PARTY, 0);
            xQueueSendFromISR(command_complete_queue, &evt, &high_task_wakeup);
            break;
        case STATE_POST_RESPONSE_DEADTIME:
            setState(STATE_WAITING_FOR_3RD_PARTY, 0);
            break;

    }
}


void dali_init() {
    // The RMT driver will turn GPIO on first, then drive it to its idle value.  To avoid an unwanted pulse, we setup the GPIO first.
    gpio_config_t gpioConfig = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = 1 << TX_PIN,
    };
    gpio_set_level(TX_PIN, 0);
    gpio_config(&gpioConfig);


    rmt_tx_channel_config_t txconfig = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = TX_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = 1 * 1000 * 1000,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&txconfig, &tx_chan));
    ESP_ERROR_CHECK(rmt_enable(tx_chan));
    ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(tx_chan, &txcb, NULL));
    ESP_ERROR_CHECK(rmt_new_dali_encoder(&tx_encoder));

    esp_err_t err = esp_timer_init();
    assert(err == ESP_OK || err == ESP_ERR_INVALID_STATE); // To allow for somebody else to have already initialised it.
    esp_timer_create_args_t timer_args = {
        .callback = rx_timer_expired,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "DALI RX timeout",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &rxTimeoutTimer));



    rmt_rx_channel_config_t rxconfig = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RX_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = 1 * 1000 * 1000,
        .flags.invert_in = false,
        .flags.io_loop_back = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rxconfig, &rx_chan));
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &rx_callbacks, NULL));
    
    ESP_ERROR_CHECK(rmt_enable(rx_chan));

    pending_cmd_queue = xQueueCreate(10, sizeof(command_t));
    command_complete_queue = xQueueCreate(1, sizeof(rx_command_complete_event_t) );

    xTaskCreate(dali_worker, "dali_worker", 8192, xTaskGetCurrentTaskHandle(), 5, NULL);
}


static inline void commandtoMSBFirstOrder(uint16_t value, uint8_t *buf) {
    *buf++ = value >> 8;
    *buf = value & 0xFF;
}

int dali_send_command(uint16_t value, TickType_t ticksToWait) {
    // Ensure the bytes are in the right order (most significant byte first)
    command_t command;
    int result = DALI_RESPONSE_TIMEOUT;
    command.command = value;
    if (ticksToWait != 0) {
        command.waiter = xSemaphoreCreateBinary();
    } else {
        command.waiter = NULL;
    }
    command.result = &result;
    // send the received RMT symbols to the parser task
    ESP_LOGD(TAG, "Enqueueing 0x%04x", value);
    if (xQueueSend(pending_cmd_queue, &command, 0) == pdFALSE) {
        ESP_LOGW(TAG, "TX Queue overflow");
        abort();
    }

    if (ticksToWait != 0) {
        if (xSemaphoreTake(command.waiter, ticksToWait) == pdTRUE) {
            // We got a result, which already should have been copied into result. 
            ESP_LOGD(TAG, "Command completed, returning 0x%02x", result);
        } else {
            ESP_LOGE(TAG, "Timeout while waiting for command response");
            // timeout
            result = DALI_RESPONSE_TIMEOUT;
        }
        vSemaphoreDelete(command.waiter);
    }
    return result;
}





static void sendCmdToDALIBus(command_t *command) {
    const rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    rx_command_complete_event_t completeEvent;
    
    uint8_t buf[2];

    commandtoMSBFirstOrder(command->command, buf);
    ESP_LOGD(TAG, "Transmitting CMD 0x%02x%02x", buf[0], buf[1]);
    
    while (state != STATE_WAITING_FOR_3RD_PARTY) {
        ESP_LOGD(TAG, "Waiting for bus to become idle");
        // Rather than use vTaskDelay which may sleep for a random amount of time, see if anybody else wants
        // to do something, then come back and test again.
        // TODO what happens if it stays stuck?  Probably should have some form of watchdog.
        taskYIELD();
    }
    setState(STATE_TRANSMITTING, 0);
    ESP_ERROR_CHECK(rmt_transmit(tx_chan, tx_encoder, buf, 2, &tx_config)); 

    // Wait for transmission to be complete.
    if (xQueueReceive(command_complete_queue, &completeEvent, pdMS_TO_TICKS(50)) != pdTRUE) {
        // This should never happen.
        ESP_LOGE(TAG, "Transmit did not complete within reasonable time. Aborting");
        abort();
    }
    // TODO perform retries upon collision, up to a maximum number of times. 
    assert(completeEvent.numBits == 0 || completeEvent.numBits == 8);
    *(command->result) = completeEvent.response;
    ESP_LOGD(TAG, "Result is %d", *(command->result));

    // Notify the caller that the command has completed.
    if (command->waiter != NULL) {
        xSemaphoreGive(command->waiter);
    }
}



static void dali_worker(void *aContext) {
    command_t command;

    esp_err_t err = rmt_receive(rx_chan, receiveBuf, sizeof(receiveBuf), &rx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Got error %d", err);
    }
    while (1) {
        ESP_LOGD(TAG, "Waiting for command");
        if (xQueueReceive(pending_cmd_queue, &command, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "Received");
            sendCmdToDALIBus(&command);
        } else {
            ESP_LOGD(TAG, "Returned false");
        }
        
    }
    vTaskDelete(NULL);
}
