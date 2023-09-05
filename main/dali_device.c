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
#include "dali_device.h"
#include "dali_provider.h"

#define TAG "dali_device"



void dali_device_init(dali_device_t *self, dali_provider_t *prov, device_serial_t *serial, uint16_t addr, uint8_t lightType, uint8_t level, uint8_t min_level, uint8_t max_level, uint8_t power_on_level, uint16_t group_membership) {
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


static ccpeed_err_t read_memory(dali_provider_t *self, uint32_t addr, unsigned int bank, unsigned int offset, uint8_t *out, size_t num) {
	if (dali_send_command(self, DALI_CMD_SET_DTR1 | bank, pdMS_TO_TICKS(500)) != DALI_RESPONSE_NAK) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	if (dali_send_command(self, DALI_CMD_SET_DTR0 | offset, pdMS_TO_TICKS(500)) != DALI_RESPONSE_NAK) {
		return CCPEED_ERROR_BUS_ERROR;
	}

    for (int offset = 0; offset < num; offset++) {
		int response = dali_send_command(self, addr | DALI_CMD_READ_MEMORY_LOCATION, pdMS_TO_TICKS(500));
		if (response >= 0) {
			*out++ = response;
		} else {
			ESP_LOGE(TAG, "Error reading memory bank byte %d", offset);
			return CCPEED_ERROR_BUS_ERROR;
		}
	}
	return CCPEED_NO_ERR;
}


dali_device_t *dali_device_find_by_addr(uint16_t addr) {
    for (device_t *dev = device_get_all(); dev != NULL; dev = (device_t *) dev->_llitem.next) {
        if (dev->provider->type == DALI_PROVIDER_ID && ((dali_device_t *) dev)->address == addr)
        return (dali_device_t *) dev;
    }
    return NULL;
}



ccpeed_err_t dali_device_update_all_attr(dali_device_t *device) {
    dali_provider_t *provider = (dali_provider_t *) device->super.provider;

	int res = dali_send_command(provider, device->address | DALI_CMD_QUERY_DEVICE_TYPE, pdMS_TO_TICKS(500));
	if (res < DALI_RESPONSE_NAK) {
		ESP_LOGW(TAG, "Error scanning for device %d", device->address);
		return CCPEED_ERROR_BUS_ERROR;
	}

	if (res == DALI_RESPONSE_NAK) {
		ESP_LOGD(TAG, "Device %u not found", device->address);
		return CCPEED_ERROR_NOT_FOUND;
	}
    ESP_LOGD(TAG, "Device %d is of type %d", DALI_ID_FROM_ADDR(device->address), res);

	device->lightType = res;
	if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_ACTUAL_LEVEL, pdMS_TO_TICKS(500))) < 0) {
        ESP_LOGW(TAG, "Didn't get response from querying Level of device %d", DALI_ID_FROM_ADDR(device->address));
		return CCPEED_ERROR_BUS_ERROR;
	}
	device->level = res;

	if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_MIN_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	device->min_level = res;
	
	if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_MAX_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	device->max_level = res;
	if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_POWER_ON_LEVEL, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	device->power_on_level = res;

	if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_GROUPS_ZERO_TO_SEVEN, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	device->group_membership = res;
	if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_GROUPS_EIGHT_TO_FIFTEEN, pdMS_TO_TICKS(500))) < 0) {
		return CCPEED_ERROR_BUS_ERROR;
	}
	device->group_membership |= res << 8;

    device->super.aspects[0] = ASPECT_ON_OFF_ID;
    device->super.aspects[1] = ASPECT_BRIGHTNESS_ID;
    device->super.num_aspects = device->min_level != device->max_level ? 2 : 1;

	return CCPEED_NO_ERR;
}


ccpeed_err_t dali_device_read_serial(dali_provider_t *provider, uint16_t addr, device_serial_t *serial) {
    ccpeed_err_t res;

  	if ((res = read_memory(provider, addr, 0, 3, serial->serial, 6)) != CCPEED_NO_ERR) {
		ESP_LOGW(TAG, "Error reading GTIN of addr %d: %d", res, DALI_ID_FROM_ADDR(addr));
		return CCPEED_ERROR_BUS_ERROR;
	};
	if ((res = read_memory(provider, addr, 0, 10, serial->serial+6, 8)) != CCPEED_NO_ERR) {
		ESP_LOGW(TAG, "Error reading serial of addr %d: %d", res, DALI_ID_FROM_ADDR(addr));
		return CCPEED_ERROR_BUS_ERROR;
	};
    serial->len = 14;
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


void dali_device_fetch_level(dali_device_t *self) {
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


ccpeed_err_t dali_device_set_attr(dali_device_t *dev, int aspect_id, int attr_id, CborValue *val) {
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
                    dali_device_fetch_level(dev);
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



ccpeed_err_t dali_device_process_service_call(dali_device_t *device, int aspectId, int serviceId, CborValue *params, size_t numParams) {
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
                    dali_device_fetch_level(device);

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
                    dali_device_fetch_level(device);
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
