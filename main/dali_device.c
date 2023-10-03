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
#include "interface_switch.h"
#include "interface_brightness.h"
#include <string.h>

#include "esp_log.h"
#include "interface_switch.h"
#include "linked_list.h"
#include "dali_device.h"
#include "dali_provider.h"
#include "cbor_helpers.h"

#define TAG "dali_device"

// Convenience for fetching provider of a dali device.
#define DALI_DEVICE_GET_PROVIDER(d) ((dali_provider_t *) d->super.provider)
#define CMD_WAIT_TIMEOUT pdMS_TO_TICKS(500)

void dali_device_init(dali_device_t *self, dali_provider_t *prov, device_identifier_t *serial, uint16_t addr, uint8_t lightType, uint8_t level, uint8_t min_level, uint8_t max_level, uint8_t power_on_level, uint16_t group_membership) {
    int aspects[2] = {
        THINGIF_SWITCH,
        THINGIF_BRIGHTNESS
    };    
    device_init(&self->super, &prov->super, serial, aspects, min_level != max_level ? 2 : 1);

    self->address = addr;
    self->lightType = lightType;
    self->brightness_attr.level = level;
    self->brightness_attr.is_level_present = true;
    self->brightness_attr.min_level = min_level;
    self->brightness_attr.is_min_level_present = true;
    self->brightness_attr.max_level = max_level;
    self->brightness_attr.is_max_level_present = true;
    self->brightness_attr.power_on_level = power_on_level;
    self->brightness_attr.is_power_on_level_present = true;
    self->group_membership = group_membership;
    self->switch_attr.is_on_change_present = false;
    self->switch_attr.is_on_present = true;
    self->switch_attr.on = level != 0;
}


static const uint16_t set_dtr_cmds[3] = {
    DALI_CMD_SET_DTR0,
    DALI_CMD_SET_DTR1,
    DALI_CMD_SET_DTR2,
};

static ccpeed_err_t dali_set_dtr(dali_provider_t *self, unsigned int dtrNum, uint8_t val) {
    if (dtrNum >= (sizeof(set_dtr_cmds)/sizeof(set_dtr_cmds[0]))) {
        return CCPEED_ERROR_INVALID;
    }
    if (dali_send_command(self, set_dtr_cmds[dtrNum] | val, CMD_WAIT_TIMEOUT) != DALI_RESPONSE_NAK) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    return CCPEED_NO_ERR;    
}


static ccpeed_err_t read_memory(dali_provider_t *self, uint32_t addr, unsigned int bank, unsigned int offset, uint8_t *out, size_t num) {

    if (dali_set_dtr(self, 1, bank) != CCPEED_NO_ERR) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    if (dali_set_dtr(self, 0, offset) != CCPEED_NO_ERR) {
        return CCPEED_ERROR_BUS_ERROR;
    }

    for (int offset = 0; offset < num; offset++) {
        int response = dali_send_command(self, addr | DALI_CMD_READ_MEMORY_LOCATION, CMD_WAIT_TIMEOUT);
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
    dali_provider_t *provider = DALI_DEVICE_GET_PROVIDER(device);

    int res = dali_send_command(provider, device->address | DALI_CMD_QUERY_DEVICE_TYPE, CMD_WAIT_TIMEOUT);
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
    if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_ACTUAL_LEVEL, CMD_WAIT_TIMEOUT)) < 0) {
        ESP_LOGW(TAG, "Didn't get response from querying Level of device %d", DALI_ID_FROM_ADDR(device->address));
        return CCPEED_ERROR_BUS_ERROR;
    }
    device->brightness_attr.is_level_present = true;
    device->brightness_attr.level = res;
    device->switch_attr.is_on_present = true;
    device->switch_attr.on = device->brightness_attr.min_level != 0;

    if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_MIN_LEVEL, CMD_WAIT_TIMEOUT)) < 0) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    device->brightness_attr.is_min_level_present = true;
    device->brightness_attr.min_level = res;
    
    if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_MAX_LEVEL, CMD_WAIT_TIMEOUT)) < 0) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    device->brightness_attr.is_max_level_present = true;
    device->brightness_attr.max_level = res;
    if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_POWER_ON_LEVEL, CMD_WAIT_TIMEOUT)) < 0) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    device->brightness_attr.power_on_level = res;
    device->brightness_attr.is_power_on_level_present = true;

    if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_GROUPS_ZERO_TO_SEVEN, CMD_WAIT_TIMEOUT)) < 0) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    device->group_membership = res;
    if ((res = dali_send_command(provider, device->address | DALI_CMD_QUERY_GROUPS_EIGHT_TO_FIFTEEN, CMD_WAIT_TIMEOUT)) < 0) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    device->group_membership |= res << 8;

    device->super.aspects[0] = THINGIF_SWITCH;
    device->super.aspects[1] = THINGIF_BRIGHTNESS;
    device->super.num_aspects = device->brightness_attr.min_level != device->brightness_attr.max_level ? 2 : 1;

    return CCPEED_NO_ERR;
}


ccpeed_err_t dali_device_read_serial(dali_provider_t *provider, uint16_t addr, device_identifier_t *ident) {
    ccpeed_err_t res;
    
    ident->num_parts = 2;
    
    ident->parts[0].type = DEVID_TYPE_GTIN;
    ident->parts[0].len = 6;
    if ((res = read_memory(provider, addr, 0, 3, ident->parts[0].data, 6)) != CCPEED_NO_ERR) {
        ESP_LOGW(TAG, "Error reading GTIN of addr %d: %d", res, DALI_ID_FROM_ADDR(addr));
        return CCPEED_ERROR_BUS_ERROR;
    };

    ident->parts[1].type = DEVID_TYPE_SERIAL_NUMBER;
    ident->parts[1].len = 8;
    if ((res = read_memory(provider, addr, 0, 11, ident->parts[1].data, 8)) != CCPEED_NO_ERR) {
        ESP_LOGW(TAG, "Error reading serial of addr %d: %d", res, DALI_ID_FROM_ADDR(addr));
        return CCPEED_ERROR_BUS_ERROR;
    };
    return CCPEED_NO_ERR;
}


ccpeed_err_t dali_device_encode_attributes(device_t *self, int aspect_id, CborEncoder *encoder) {
    dali_device_t *dev = (dali_device_t *) self;
    char buf[60];
    ccpeed_err_t err;

    ESP_LOGI(TAG, "Encoding attributes for device %s, aspect %d", device_identifier_to_str(&dev->super.id, buf, sizeof(buf)), aspect_id);
    switch (aspect_id) {
        case THINGIF_SWITCH:
            err = thingif_switch_attr_write(&dev->switch_attr, encoder);
            if (err != CCPEED_NO_ERR) {
                return err;
            }
            break;
        case THINGIF_BRIGHTNESS:
            err = thingif_brightness_attr_write(&dev->brightness_attr, encoder);
            if (err != CCPEED_NO_ERR) {
                return err;
            }
            break;

        default:
            return CCPEED_ERROR_INVALID;
    }
    return CCPEED_NO_ERR;

}

static ccpeed_err_t dali_device_query(dali_device_t *self, uint16_t query_cmd, uint8_t *out) {
    int ret = dali_send_command(DALI_DEVICE_GET_PROVIDER(self), self->address | query_cmd, CMD_WAIT_TIMEOUT);

    if (ret >= 0) {
        *out = ret;
        return CCPEED_NO_ERR;
    }

    return CCPEED_ERROR_BUS_ERROR;
}


static ccpeed_err_t dali_device_fetch_level(dali_device_t *self) {
    char buf[60];
    int res = dali_send_command((dali_provider_t *)self->super.provider, self-> address | DALI_CMD_QUERY_ACTUAL_LEVEL, pdMS_TO_TICKS(200));
    if (res >= 0) {
        ESP_LOGI(TAG, "Device %s level is now %d", device_identifier_to_str(&self->super.id, buf, sizeof(buf)), res);
        self->brightness_attr.level = res;
        self->brightness_attr.is_level_present = true;
        self->switch_attr.on = res != 0;
        self->switch_attr.is_on_present = true;
        // TODO notify listeners of new level
        return CCPEED_NO_ERR;
    } else {
        ESP_LOGW(TAG, "Error fetching level: %d", res);
        return CCPEED_ERROR_BUS_ERROR;
    }
}


static ccpeed_err_t dali_device_wait_for_fade(dali_device_t *self) {
    ccpeed_err_t err;
    uint8_t status = 0xFF;

    // TODO If device doesn't support fading, we can short-cut.


    // Wait for fade to complete. 
    bool fade_running = true;
    do {
        err = dali_device_query(self, DALI_CMD_QUERY_STATUS, &status);
        if (err != CCPEED_NO_ERR) {
            return err;
        }
        fade_running = status & DALI_DEVICE_STATUS_FADE_RUNNING;
        if (fade_running) {
            vTaskDelay(1);
        }
    } while (fade_running);
    return CCPEED_NO_ERR;
}


static ccpeed_err_t dali_device_send_command(dali_device_t *device, uint16_t cmd) {
    int ret = dali_send_command(DALI_DEVICE_GET_PROVIDER(device), device->address | cmd, CMD_WAIT_TIMEOUT);
    switch (ret) {
        case DALI_RESPONSE_NAK:
            return CCPEED_NO_ERR;
        default:
            return CCPEED_ERROR_BUS_ERROR;
    }
}



static ccpeed_err_t dali_send_dapc_command(dali_device_t *device, uint8_t level) {
    if (level == 255) {
        return CCPEED_ERROR_INVALID;
    }
    return dali_send_command(DALI_DEVICE_GET_PROVIDER(device), device->address | level, CMD_WAIT_TIMEOUT);
}


static ccpeed_err_t dali_device_set_numeric_config(dali_device_t *dev, uint16_t cmd, uint8_t val) {
    char buf[60];
    ccpeed_err_t cerr;

    if (dali_set_dtr(DALI_DEVICE_GET_PROVIDER(dev), 0, val) != CCPEED_NO_ERR) {
        return CCPEED_ERROR_BUS_ERROR;
    }
    cerr = dali_device_send_command(dev, cmd);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }
    // Configuration commands must be sent twice. 
    cerr = dali_device_send_command(dev, cmd);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }
    ESP_LOGI(TAG, "device %s.%d set to %d", device_identifier_to_str(&dev->super.id, buf, sizeof(buf)), cmd, val);
    return CCPEED_NO_ERR;
}

static ccpeed_err_t dali_device_set_is_on(dali_device_t *self, bool val) {
    char buf[60];
    ccpeed_err_t cerr;

    cerr = dali_device_send_command(self, val ? DALI_CMD_GOTO_LAST_ACTIVE_LEVEL : DALI_CMD_OFF);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }
    ESP_LOGI(TAG, "device %s is_on set to %s", device_identifier_to_str(&self->super.id, buf, sizeof(buf)), val ? "true" : "false");
    return CCPEED_NO_ERR;
}

ccpeed_err_t dali_device_set_switch_attr(dali_device_t *dev, thingif_switch_attr_t *swattr) {
    ccpeed_err_t cerr;

    if (swattr->is_on_present) {
        dali_device_set_is_on(dev, swattr->on);
    }
    cerr = dali_device_wait_for_fade(dev);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }
    cerr = dali_device_fetch_level(dev);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }

    thingif_switch_attr_copy(&dev->switch_attr, swattr);

    // Each time, after we set attributes, check the current level afterwards.
    cerr = dali_device_wait_for_fade(dev);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }
    cerr = dali_device_fetch_level(dev);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }

    return CCPEED_NO_ERR;
}

ccpeed_err_t dali_device_set_brightness_attr(dali_device_t *dev, thingif_brightness_attr_t *brattr) {
    ccpeed_err_t cerr;

    if (brattr->is_level_present) {
        cerr = dali_send_dapc_command(dev, brattr->level);
        if (cerr != CCPEED_NO_ERR) {
            return cerr;
        }
    }

    if (brattr->is_max_level_present) {
        cerr = dali_device_set_numeric_config(dev, DALI_CMD_SET_MAX_LEVEL, brattr->max_level);
        if (cerr != CCPEED_NO_ERR) {
            return cerr;
        }
    }

    if (brattr->is_min_level_present) {
        cerr = dali_device_set_numeric_config(dev, DALI_CMD_SET_MIN_LEVEL, brattr->min_level);
        if (cerr != CCPEED_NO_ERR) {
            return cerr;
        }
    }

    if (brattr->is_power_on_level_present) {
        cerr = dali_device_set_numeric_config(dev, DALI_CMD_SET_POWER_ON_LEVEL, brattr->power_on_level);
        if (cerr != CCPEED_NO_ERR) {
            return cerr;
        }
    }
    thingif_brightness_attr_copy(&dev->brightness_attr, brattr);

    // Each time, after we set attributes, check the current level afterwards.
    cerr = dali_device_wait_for_fade(dev);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }
    cerr = dali_device_fetch_level(dev);
    if (cerr != CCPEED_NO_ERR) {
        return cerr;
    }
    return CCPEED_NO_ERR;
}



ccpeed_err_t dali_device_set_attr(device_t *self, int aspect_id, CborValue *val) {
    dali_device_t *dev = (dali_device_t *) self;
    ccpeed_err_t cerr;
    thingif_switch_attr_t swattr;
    thingif_brightness_attr_t brattr;

    switch (aspect_id) {
        case THINGIF_SWITCH:
            cerr = thingif_switch_attr_read(&swattr, val);
            if (cerr != CCPEED_NO_ERR) {
                return cerr;
            }
            cerr = dali_device_set_switch_attr(dev, &swattr);
            if (cerr != CCPEED_NO_ERR) {
                return cerr;
            }
            break;
        case THINGIF_BRIGHTNESS:
            cerr = thingif_brightness_attr_read(&brattr, val);
            if (cerr != CCPEED_NO_ERR) {
                return cerr;
            }
            cerr = dali_device_set_brightness_attr(dev, &brattr);

            break;
        default:
            return CCPEED_ERROR_NOT_FOUND;
    }


    return CCPEED_NO_ERR;
}



ccpeed_err_t dali_device_process_service_call(device_t *self, int aspectId, int serviceId, CborValue *params, size_t numParams) {
    dali_device_t *device = (dali_device_t *) self;
    CborError err;
    char buf[60];
    ccpeed_err_t cerr;

    switch (aspectId) {
        case THINGIF_SWITCH:
            switch (serviceId) {
                case THINGIF_SWITCH_OP_TOGGLE_ON:
                    if (numParams != 0) {
                        ESP_LOGW(TAG, "Toggle has no parameters");
                        return CCPEED_ERROR_INVALID;
                    }
                    ESP_LOGI(TAG, "Device %s toggling is_on", device_identifier_to_str(&device->super.id, buf, sizeof(buf)));
                    if (device->brightness_attr.level) {
                        cerr = dali_device_send_command(device, DALI_CMD_OFF);
                        if (cerr != CCPEED_NO_ERR) {
                            return cerr;
                        }

                        device->brightness_attr.level = 0;
                    } else {
                        cerr = dali_device_send_command(device, DALI_CMD_GOTO_LAST_ACTIVE_LEVEL);
                        if (cerr != CCPEED_NO_ERR) {
                            return cerr;
                        }

                    }
                    cerr = dali_device_wait_for_fade(device) || dali_device_fetch_level(device);
                    if (cerr != CCPEED_NO_ERR) {
                        return cerr;
                    }
                    return CCPEED_NO_ERR;
                default:
                    return CCPEED_ERROR_NOT_FOUND;
            }
            break;
        case THINGIF_BRIGHTNESS:
            switch (serviceId) {
                case THINGIF_BRIGHTNESS_OP_DELTA:
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
                    if (!device->brightness_attr.level) {
                        cerr = dali_device_send_command(device, DALI_CMD_GOTO_LAST_ACTIVE_LEVEL);
                        if (cerr != CCPEED_NO_ERR) {
                            return cerr;
                        }
                    }
                    if (delta > 0) {
                        cerr = dali_device_send_command(device, DALI_CMD_UP);
                        if (cerr != CCPEED_NO_ERR) {
                            return cerr;
                        }
                    } else if (delta < 0) {
                        cerr = dali_device_send_command(device, DALI_CMD_DOWN);
                        if (cerr != CCPEED_NO_ERR) {
                            return cerr;
                        }
                    } else {
                        // Delta is zero, which is silly
                        ESP_LOGW(TAG, "Attempt to apply delta of 0");             
                        return CCPEED_ERROR_INVALID;
                    }
                    cerr = dali_device_wait_for_fade(device) || dali_device_fetch_level(device);
                    if (cerr != CCPEED_NO_ERR) {
                        return cerr;
                    }

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
