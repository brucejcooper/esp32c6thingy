#include "dali_provider.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "provider.h"
#include "device.h"
#include "aspect_on_off.h"
#include "aspect_brightness.h"
#include <string.h>

#include "esp_log.h"
#include "aspect_on_off.h"
#include "linked_list.h"


#define DALI_BIT_USEC 833
#define DALI_HALF_BIT_USEC (DALI_BIT_USEC/ 2)
// We allow 25% of a half bit tolerance either side for timing. 
#define TIMING_ALLOWANCE 105
#define IS_HALF_BIT(x) (x > (DALI_HALF_BIT_USEC-TIMING_ALLOWANCE) && x < (DALI_HALF_BIT_USEC+TIMING_ALLOWANCE))
#define IS_FULL_BIT(x) (x > (DALI_BIT_USEC-TIMING_ALLOWANCE) && x < (DALI_BIT_USEC+TIMING_ALLOWANCE))




#define TAG "dali_provider"

static rmt_encoder_handle_t tx_encoder;

const rmt_receive_config_t rx_config = {
    .signal_range_min_ns = 2000,
    .signal_range_max_ns = (3*833/2) * 1000,
};


typedef struct {
    uint16_t duration : 15; /*!< Duration of level0 */
    uint16_t level : 1;     /*!< Level of the first part */
} single_pulse_t;

typedef struct {
    uint16_t command;
    SemaphoreHandle_t waiter;
    int *result;
} command_t;



static bool rx_transaction_done(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx);
static bool tx_transaction_done(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx);
static void dali_transcieve_worker(void *aContext);

static const rmt_rx_event_callbacks_t rx_callbacks = {
    .on_recv_done = rx_transaction_done
};

static const rmt_tx_event_callbacks_t txcb = {
    .on_trans_done = tx_transaction_done
};



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


static void setState(dali_provider_t *self, dali_state_t newState, uint64_t timeout_usec) {
    assert(self != NULL);
    state = newState;
    // Disable any existing timeout
    // Don't check response from this one, as the only possible outcomes are OK or already stopepd. 
    esp_timer_stop(self->rxTimeoutTimer);
    if (timeout_usec) {
        // Set timeout.
        ESP_ERROR_CHECK(esp_timer_start_once(self->rxTimeoutTimer, timeout_usec));
    }
}

static bool rx_transaction_done(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_wakeup = pdFALSE;
    rx_command_complete_event_t evt;
    uint32_t data;
    esp_err_t err;
    dali_provider_t *self = user_ctx;

    // immediately stop the read timeout, to avoid race conditions between the timer and the RMT receiver.
    esp_timer_stop(self->rxTimeoutTimer);

    evt.numBits = reconstructDaliSignal(edata, (uint32_t *) &data);
    // Allow receive to start again (always be receiving)
    err = rmt_receive(rx_chan, self->receiveBuf, sizeof(self->receiveBuf), &rx_config);
    if (err != ESP_OK) {
        abort();
    }

    // receiving proceeds in a loop.
    switch (state) {
        case STATE_WAITING_FOR_3RD_PARTY:
            // This is a 3rd party command, wait for the response as well. 
            // No need to send an event.
            setState(self, STATE_WAITING_FOR_RESPONSE, 20000);
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
            setState(self, STATE_WAITING_FOR_RESPONSE, 20000);
            break;
        case STATE_WAITING_FOR_RESPONSE:
            // after receiving a reverse frame, we must wait at least 6 half bit periods (approx 2.5ms) before transmitting again.
            setState(self, STATE_POST_RESPONSE_DEADTIME, 2500);
            if (evt.numBits == 8) {
                evt.response = (int) data;
                xQueueSendFromISR(self->command_complete_queue, &evt, &high_task_wakeup);
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
    dali_provider_t *self = user_ctx;

    BaseType_t high_task_wakeup = pdFALSE;
    // This only ever happens after transmit of a command has just completed.  This means 
    assert(state == STATE_TRANSMITTING);
    setState(self, STATE_WAITING_FOR_READBACK, 2200);
    // Set the receive timeout
    // xQueueSendFromISR(rx_wait_queue, &evt, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void rx_timer_expired(void *args) {
    rx_command_complete_event_t evt;
    BaseType_t high_task_wakeup = pdFALSE;
    dali_provider_t *self = args;
    assert(self != NULL);

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
            setState(self, STATE_WAITING_FOR_3RD_PARTY, 0);
            xQueueSendFromISR(self->command_complete_queue, &evt, &high_task_wakeup);
            break;
        case STATE_COLLISION:
            // Collisions happen while transmitting.  We have cleared the state now, so this means that our transmit failed.  Let
            // the outer loop know. 
            evt.numBits = 0;
            evt.response = DALI_RESPONSE_COLLISION;
            setState(self, STATE_WAITING_FOR_3RD_PARTY, 0);
            xQueueSendFromISR(self->command_complete_queue, &evt, &high_task_wakeup);
            break;
        case STATE_POST_RESPONSE_DEADTIME:
            setState(self, STATE_WAITING_FOR_3RD_PARTY, 0);
            break;

    }
}


static inline void commandtoMSBFirstOrder(uint16_t value, uint8_t *buf) {
    *buf++ = value >> 8;
    *buf = value & 0xFF;
}



ccpeed_err_t dali_send_command(dali_provider_t *self, uint16_t value, TickType_t ticksToWait) {

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
    if (xQueueSend(self->pending_cmd_queue, &command, 0) == pdFALSE) {
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





static void sendCmdToDALIBus(dali_provider_t *self, command_t *command) {
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
    setState(self, STATE_TRANSMITTING, 0);
    ESP_ERROR_CHECK(rmt_transmit(self->tx_chan, tx_encoder, buf, 2, &tx_config)); 

    // Wait for transmission to be complete.
    if (xQueueReceive(self->command_complete_queue, &completeEvent, pdMS_TO_TICKS(50)) != pdTRUE) {
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



static void dali_transcieve_worker(void *aContext) {
    command_t command;
    dali_provider_t *self = aContext;

    esp_err_t err = rmt_receive(self->rx_chan, self->receiveBuf, sizeof(self->receiveBuf), &rx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Got error %d", err);
    }
    while (1) {
        ESP_LOGD(TAG, "Waiting for command");
        if (xQueueReceive(self->pending_cmd_queue, &command, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "Received");
            sendCmdToDALIBus(self, &command);
        } else {
            ESP_LOGD(TAG, "Returned false");
        }
        
    }
    vTaskDelete(NULL);
}




static int read_memory(dali_provider_t *self, uint32_t addr, unsigned int bank, unsigned int offset, uint8_t *out, size_t num) {
	if (dali_send_command(self, DALI_CMD_SET_DTR1 | bank, pdMS_TO_TICKS(500)) != DALI_RESPONSE_NAK) {
		return 1;
	};
	if (dali_send_command(self, DALI_CMD_SET_DTR0 | offset, pdMS_TO_TICKS(500)) != DALI_RESPONSE_NAK) {
		return 1;
	}

	while (num--) {
		int response = dali_send_command(self, addr | DALI_CMD_READ_MEMORY_LOCATION, pdMS_TO_TICKS(500));
		if (response >= 0) {
			*out++ = response;
		} else {
			ESP_LOGE(TAG, "Error reading memory bank");
			return 2;
		}
	}
	return 0;
}


static void dali_device_init(dali_device_t *self, dali_provider_t *prov, device_serial_t *serial, uint16_t addr, uint8_t lightType, uint8_t level, uint8_t min_level, uint8_t max_level, uint8_t power_on_level, uint16_t group_membership) {
    int aspects[2] = {
        ASPECT_ON_OFF_ID,
        ASPECT_BRIGHTNESS_ID
    };    
    device_init(&self->super, &prov->super, serial, aspects, min_level != max_level ? 2 : 1);

    self->address = addr;
    self->lightType = lightType;
    self->level = level;
    self->min_level = min_level;
    self->max_level = max_level;
    self->power_on_level = power_on_level;
    self->group_membership = group_membership;
}


static ccpeed_err_t query_dali_device(dali_provider_t *self, uint8_t logicalAddr) {
    char deviceIdStr[41];
    device_serial_t serial;
    uint16_t address;
    uint8_t lightType;
    uint8_t level;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t power_on_level;
    uint16_t group_membership;

    address = DALI_GEAR_ADDR(logicalAddr);

	int res = dali_send_command(self, address | DALI_CMD_QUERY_DEVICE_TYPE, pdMS_TO_TICKS(500));
	if (res < DALI_RESPONSE_NAK) {
		ESP_LOGW(TAG, "Error scanning for device %d", logicalAddr);
		return CCPEED_ERROR_BUS_ERROR;
	}

	if (res == DALI_RESPONSE_NAK) {
		ESP_LOGD(TAG, "Device %u not found", logicalAddr);
		return CCPEED_ERROR_NOT_FOUND;
	}
    ESP_LOGI(TAG, "Device %d is of type %d", logicalAddr, res);

	lightType = res;
	if ((res = dali_send_command(self, address | DALI_CMD_QUERY_ACTUAL_LEVEL, pdMS_TO_TICKS(500))) < 0) {
        ESP_LOGW(TAG, "Didn't get response from querying Level of device %d", logicalAddr);
		return CCPEED_ERROR_BUS_ERROR;
	}
	level = res;

	if ((res = dali_send_command(self, address | DALI_CMD_QUERY_MIN_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	min_level = res;
	
	if ((res = dali_send_command(self, address | DALI_CMD_QUERY_MAX_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	max_level = res;
	if ((res = dali_send_command(self, address | DALI_CMD_QUERY_POWER_ON_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	power_on_level = res;

	if ((res = dali_send_command(self, address | DALI_CMD_QUERY_GROUPS_ZERO_TO_SEVEN, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	group_membership = res;
	if ((res = dali_send_command(self, address | DALI_CMD_QUERY_GROUPS_EIGHT_TO_FIFTEEN, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	group_membership |= res << 8;

    // DALI unique identifier is a concatenation of its GTIN (6 bytes at offset 3) and its serial (8 bytes at offset 10)
	if ((res = read_memory(self, address, 0, 3, serial.serial, 6))) {
		ESP_LOGW(TAG, "Error reading memory bank 0: %d", res);
		return CCPEED_ERROR_BUS_ERROR;
	};
	if ((res = read_memory(self, address, 0, 10, serial.serial+6, 8))) {
		ESP_LOGW(TAG, "Error reading memory bank 0: %d", res);
		return CCPEED_ERROR_BUS_ERROR;
	};
    serial.len = 14;


    deviceIdToStr(&serial, deviceIdStr);

    device_t *existing_device = find_device(&serial);

    if (!existing_device) {
        dali_device_t *dev = malloc(sizeof(dali_device_t));
        if (dev == NULL) {
            ESP_LOGE(TAG, "Could not allocate device memory");
            return CCPEED_ERROR_NOMEM;
        }
        dali_device_init(dev, self, &serial, address, lightType, level, min_level, max_level, power_on_level, group_membership);
        existing_device = (device_t *) dev;
        add_device(existing_device);
        ESP_LOGI(TAG, "Created device %s", deviceIdStr);
    } else {
        ESP_LOGI(TAG, "Device %s already registered", deviceIdStr);
    }

    ESP_LOGI(TAG, "Scanned device %s at addr %d type %d, level %d range(%d,%d) groups 0x%x", 
                    deviceIdStr, 
                    logicalAddr, 
                    lightType, 
                    level, 
                    min_level, 
                    max_level, 
                    group_membership
    );
	return CCPEED_NO_ERR;
}


ccpeed_err_t scanDaliBus(dali_provider_t *provider) {

    ESP_LOGI(TAG, "Scanning for DALI Devices");
    // TODO delete devices that are no longer found (blue paint)
    // TODO consider short cut for devices that are already allocated. 
    for (int i = 0; i < 64; i++) {
        ccpeed_err_t err = query_dali_device(provider, i);
        if (err != CCPEED_NO_ERR) {
            return err;
        }
    }
    ESP_LOGI(TAG, "Finished scan");
    return CCPEED_NO_ERR;
}

static void scanDaliBusTask(void *params) {
    dali_provider_t *provider = params;
    scanDaliBus(provider);
    vTaskDelete(NULL);
}


CborError cbor_value_get_uint32(CborValue *val, uint32_t *out) {
    uint64_t tmp;

    CborError err = cbor_value_get_uint64(val, &tmp);
    if (err == CborNoError) {
        *out = (uint32_t) tmp;
    }
    return err;
}


ccpeed_err_t dali_provider_init(dali_provider_t *self, CborValue *it) {
    provider_init(&self->super, DALI_PROVIDER_ID);
    CborError err;

    if (!cbor_value_is_unsigned_integer(it)) {
        ESP_LOGE(TAG, "First parameter to DALI provider must be an Integer for TX pin");
        return CCPEED_ERROR_INVALID;
    }
    err = cbor_value_get_uint32(it, &self->tx_pin);
    if (err) {
        return CCPEED_ERROR_INVALID;
    }
    err = cbor_value_advance(it);
    if (err) {
        return CCPEED_ERROR_INVALID;
    }
    
    if (!cbor_value_is_unsigned_integer(it)) {
        ESP_LOGE(TAG, "Second parameter to DALI provider must be an Integer for RX pin");
        return CCPEED_ERROR_INVALID;
    }
    err = cbor_value_get_uint32(it, &self->rx_pin);
    if (err) {
        return CCPEED_ERROR_INVALID;
    }
    err = cbor_value_advance(it);
    if (err) {
        return CCPEED_ERROR_INVALID;
    }


    // Set up IO
        // The RMT driver will turn GPIO on first, then drive it to its idle value.  To avoid an unwanted pulse, we setup the GPIO first.
    gpio_config_t gpioConfig = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = 1 << self->tx_pin,
    };
    gpio_set_level(self->tx_pin, 0);
    gpio_config(&gpioConfig);


    rmt_tx_channel_config_t txconfig = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = self->tx_pin,
        .mem_block_symbols = 64,
        .resolution_hz = 1 * 1000 * 1000,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&txconfig, &self->tx_chan));
    ESP_ERROR_CHECK(rmt_enable(self->tx_chan));
    ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(self->tx_chan, &txcb, self));
    ESP_ERROR_CHECK(rmt_new_dali_encoder(&tx_encoder));

    esp_err_t eerr = esp_timer_init();
    assert(eerr == ESP_OK || eerr == ESP_ERR_INVALID_STATE); // To allow for somebody else to have already initialised it.
    esp_timer_create_args_t timer_args = {
        .callback = rx_timer_expired,
        .arg = self,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "DALI RX timeout",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &self->rxTimeoutTimer));



    rmt_rx_channel_config_t rxconfig = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = self->rx_pin,
        .mem_block_symbols = 64,
        .resolution_hz = 1 * 1000 * 1000,
        .flags.invert_in = false,
        .flags.io_loop_back = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rxconfig, &self->rx_chan));
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(self->rx_chan, &rx_callbacks, self));
    
    ESP_ERROR_CHECK(rmt_enable(self->rx_chan));

    self->pending_cmd_queue = xQueueCreate(10, sizeof(command_t));
    self->command_complete_queue = xQueueCreate(1, sizeof(rx_command_complete_event_t) );


    // Set up queues and tasks.



    xTaskCreate(dali_transcieve_worker, "dali_transcieve_worker", 8192, self, 5, &self->transcieve_task);

    // Schedule an initial scan 
    ESP_LOGI(TAG, "Scheduling an initial DALI bus scan");
    xTaskCreate(scanDaliBusTask, "dali_bus_scanner", 4096, self, 4, NULL);

    ESP_LOGI(TAG, "Configured DALI provider TX: %lu, RX: %lu", self->tx_pin, self->rx_pin);
    return CCPEED_NO_ERR;
}



ccpeed_err_t dali_device_encode_attributes(dali_device_t *dev, int aspect_id, CborEncoder *encoder) {
    CborEncoder attributeEncoder;
    switch (aspect_id) {
        case ASPECT_ON_OFF_ID:
            cbor_encoder_create_map(encoder, &attributeEncoder, 1);
            cbor_encode_uint(&attributeEncoder, ASPECT_ON_OFF_ATTR_IS_ON_ID);
            cbor_encode_boolean(&attributeEncoder, dev->level != 0);
            cbor_encoder_close_container(encoder, &attributeEncoder);

            break;
        case ASPECT_BRIGHTNESS_ID:
            cbor_encoder_create_map(encoder, &attributeEncoder, 3);
            cbor_encode_uint(&attributeEncoder, ASPECT_BRIGHTNESS_ATTR_LEVEL_ID);
            cbor_encode_uint(&attributeEncoder, dev->level);

            cbor_encode_uint(&attributeEncoder, ASPECT_BRIGHTNESS_ATTR_MIN_LEVEL_ID);
            cbor_encode_uint(&attributeEncoder, dev->min_level);

            cbor_encode_uint(&attributeEncoder, ASPECT_BRIGHTNESS_ATTR_MAX_LEVEL_ID);
            cbor_encode_uint(&attributeEncoder, dev->max_level);

            cbor_encoder_close_container(encoder, &attributeEncoder);
            break;
    }
    return CCPEED_NO_ERR;

}


static void fetchCurrentLightLevel(dali_device_t *self) {
    int res = dali_send_command((dali_provider_t *)self->super.provider, self-> address | DALI_CMD_QUERY_ACTUAL_LEVEL, pdMS_TO_TICKS(200));
    if (res >= 0) {
        ESP_LOGI(TAG, "Level is now %d", res);
        self->level = res;
        // TODO notify listeners of new level
    } else {
        ESP_LOGW(TAG, "Error fetching level: %d", res);
    }
}


int dali_device_send_command(dali_device_t *device, uint16_t cmd, TickType_t timeToWait) {
    return dali_send_command((dali_provider_t *) device->super.provider, device->address | cmd, timeToWait);
}


ccpeed_err_t dali_set_attr(dali_device_t *dev, int aspect_id, int attr_id, CborValue *val) {
    int ival;
    CborError err;

    switch (aspect_id) {
        case ASPECT_ON_OFF_ID:
            switch (attr_id) {
                case ASPECT_ON_OFF_ATTR_IS_ON_ID:
                    if (!cbor_value_is_boolean(val)) {
                        ESP_LOGW(TAG, "Light attribute IS_ON only accepts booleans");
                        return CborErrorIllegalType;
                    }
                    bool is_on;
                    err = cbor_value_get_boolean(val, &is_on);
                    if (err != CborNoError) {
                        return CCPEED_ERROR_INVALID;
                    }

                    ESP_LOGI(TAG, "dali light.is_on set to %d", is_on);

                    if (is_on) {
                        dali_device_send_command(dev, DALI_CMD_GOTO_LAST_ACTIVE_LEVEL, 0);
                    } else {
                        dali_device_send_command(dev, DALI_CMD_OFF, 0);
                    }
                    fetchCurrentLightLevel(dev);
                    break;
                default:
                    return CCPEED_ERROR_NOT_FOUND;
            }
            break;
        case ASPECT_BRIGHTNESS_ID:
            switch (attr_id) {
                case ASPECT_BRIGHTNESS_ATTR_LEVEL_ID:
                    if (!cbor_value_is_unsigned_integer(val)) {
                        ESP_LOGW(TAG, "Light attribute level only accepts unsigned integers");
                        return CborErrorIllegalType;
                    }
                    err = cbor_value_get_int_checked(val, &ival);
                    dev->level = ival;
                    ESP_LOGI(TAG, "light.level set to %d", dev->level);
                    break;
                case ASPECT_BRIGHTNESS_ATTR_MIN_LEVEL_ID:
                    if (!cbor_value_is_unsigned_integer(val)) {
                        ESP_LOGW(TAG, "Light attribute min_level only accepts unsigned integers");
                        return CborErrorIllegalType;
                    }

                    err = cbor_value_get_int_checked(val, &ival);
                    dev->min_level = ival;
                    ESP_LOGI(TAG, "light.min_level set to %d", dev->min_level);
                    break;
                case ASPECT_BRIGHTNESS_ATTR_MAX_LEVEL_ID:
                    if (!cbor_value_is_unsigned_integer(val)) {
                        ESP_LOGW(TAG, "Light attribute max_level only accepts unsigned integers");
                        return CborErrorIllegalType;
                    }
                    err = cbor_value_get_int_checked(val, &ival);
                    dev->max_level = ival;
                    ESP_LOGI(TAG, "light.max_level set to %d", dev->max_level);
                    break;
                case ASPECT_BRIGHTNESS_ATTR_POWER_ON_LEVEL_ID:
                    break;
                default:
                    return CCPEED_ERROR_NOT_FOUND;
            }
            break;
        default:
            return CCPEED_ERROR_NOT_FOUND;
    }
    return CCPEED_NO_ERR;
}



ccpeed_err_t dali_process_service_call(dali_device_t *device, int aspectId, int serviceId, CborValue *params, size_t numParams) {
    CborError err;

    switch (aspectId) {
        case ASPECT_ON_OFF_ID:
            switch (serviceId) {
                case ASPECT_ON_OFF_SERVICE_TOGGLE_ID:
                    if (numParams != 0) {
                        ESP_LOGW(TAG, "Toggle has no parameters");
                        return CCPEED_ERROR_INVALID;
                    }
                    ESP_LOGI(TAG, "Toggling is_on");
                    if (device->level) {
                        dali_device_send_command(device, DALI_CMD_OFF, 0);
                        device->level = 0;
                    } else {
                        dali_device_send_command(device, DALI_CMD_GOTO_LAST_ACTIVE_LEVEL, 0);
                    }
                    fetchCurrentLightLevel(device);

                    return CCPEED_NO_ERR;

                default:
                    return CCPEED_ERROR_NOT_FOUND;
            }
            break;
        case ASPECT_BRIGHTNESS_ID:
            switch (serviceId) {
                case ASPECT_BRIGHTNESS_SERVICE_DELTA_ID:
                    if (numParams != 1) {
                        ESP_LOGW(TAG, "apply_brightness_delta takes exactly one argument");
                        return CCPEED_ERROR_INVALID;
                    }
                    if (!cbor_value_is_integer(params)) {
                        ESP_LOGW(TAG, "Param must be an integer");
                        return CCPEED_ERROR_INVALID;
                    }
                    int delta;
                    err = cbor_value_get_int_checked(params, &delta);
                    if (err != CborNoError) {                        
                        ESP_LOGW(TAG, "Error parsing delta");
                        return CCPEED_ERROR_INVALID;
                    }
                    err = cbor_value_advance(params);
                    if (err != CborNoError) {           
                        ESP_LOGW(TAG, "Error getting attr %d", err);             
                        return CCPEED_ERROR_INVALID;
                    }

                    ESP_LOGI(TAG, "Applying delta of %d", delta);

                    // Parameters parsed successfully.  Proceed to make the change. 
                    if (!device->level) {
                        dali_device_send_command(device, DALI_CMD_GOTO_LAST_ACTIVE_LEVEL, 0);
                    }
                    if (delta > 0) {
                        dali_device_send_command(device, DALI_CMD_UP, 0);
                    } else if (delta < 0) {
                        dali_device_send_command(device, DALI_CMD_DOWN, 0);
                    } else {
                        // Delta is zero, which is silly
                        ESP_LOGW(TAG, "Attempt to apply delta of 0");             
                        return CCPEED_ERROR_INVALID;
                    }
                    fetchCurrentLightLevel(device);
                    break;
                default:
                    return CCPEED_ERROR_NOT_FOUND;
            }
            break;
        default:
            return CCPEED_ERROR_NOT_FOUND;
    }
    return CCPEED_NO_ERR;
}
