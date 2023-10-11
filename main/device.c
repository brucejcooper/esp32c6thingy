#include <stddef.h>
#include <stdint.h>
#include "device.h"
#include "cbor_helpers.h"
#include "esp_log.h"

// static const char *TAG = "device";


static const char *device_id_type_names[] = {
    "idx",
    "gpio",
    "sn",
    "mac",
    "gtin",
};

static linked_list_t devices = {
    .head = NULL,
};


device_t *device_get_all() {
    return (device_t *) devices.head;
}


void add_device(device_t *dev) {
    ll_append(&devices, &dev->_llitem);
}


void device_init(device_t *dev, provider_base_t *provider, device_identifier_t *serial, int *aspects, size_t num_apsects) {
    ll_init_item(&dev->_llitem);
    dev->provider = provider;
    memcpy(&(dev->id), serial, sizeof(device_identifier_t));
    memcpy(dev->aspects, aspects, num_apsects*sizeof(int));
    dev->num_aspects = num_apsects;
}


/**
 * @brief Determines if two serial numbers are equal.
 * 
 * @param s1 
 * @param s2 
 * @return true 
 * @return false 
 */
bool device_identifier_equals(device_identifier_t *s1, device_identifier_t *s2) {
    if (s1->num_parts != s2->num_parts) {
        return false;
    }

    for (int i = 0; i < s1->num_parts; i++) {
        ccpeed_device_identifier_part_t *p1 = s1->parts+i;
        ccpeed_device_identifier_part_t *p2 = s2->parts+i;

        if (p1->type != p2->type) {
            return false;
        }

        if (!(p1->len == p2->len && memcmp(p1->data, p2->data, p1->len) == 0)) {
            return false;
        }
    }
    return true;
}


device_t *device_find_by_id(device_identifier_t *id) {
    for (device_t *d = (device_t *) devices.head; d != NULL; d = (device_t *) d->_llitem.next) {
        if (device_identifier_equals(id, &d->id)) {
            return d;
        }
    }
    return NULL;
}

/**
 * @brief Destructor for devices. Removes the device from the "all_devices" list, and frees its memory. 
 * 
 * @param dev 
 */
void device_delete(device_t *dev) {
    ll_remove(&devices, dev);
    free(dev);
}

static inline char hexToChar(unsigned int val) {
    assert(val < 16);
    if (val < 10) {
        return '0' + val;
    } else {
        return 'A' - 10 + val;
    } 
}


char *device_identifier_to_str(device_identifier_t *serial, char *out, size_t sz) {
    char *ptr = out;
    uint8_t *bufptr;
    int res;

    ccpeed_device_identifier_part_t *part = serial->parts;
    sz--; // An initial decrement for the trailing \0
    for (int level = 0; level < serial->num_parts; level++, part++) {
        if (sz == 0) {
            return "TOOLONG";
        }
        if (level != 0) {
            *ptr++ = '/';
            sz--;
        }
        if (part->type >= sizeof(device_id_type_names)/sizeof(device_id_type_names[0])) {
            return "INVALID";
        }
        res = snprintf(ptr, sz, "%s:", device_id_type_names[part->type]);
        if (res < 0) {
            // Ran out of space;
            return "TOOLONG";
        }
        ptr += res;
        sz -= res;

        switch(part->type) {
            case DEVID_TYPE_INDEX:
            case DEVID_TYPE_GPIOPIN:
                res = snprintf(ptr, sz, "%d", part->data[0]);
                if (res < 0) {
                    // Ran out of space;
                    return "TOOLONG";
                }
                ptr += res;
                sz -= res;
                break;
            case DEVID_TYPE_SERIAL_NUMBER:
            case DEVID_TYPE_MAC:
            case DEVID_TYPE_GTIN:
                bufptr = part->data;
                for (int i = 0; i < part->len; i++) {
                    if (sz < 2) {
                        return "TOOLONG";
                    }
                    *ptr++ = hexToChar((*bufptr) >> 4); 
                    *ptr++ = hexToChar((*bufptr++) & 0x0F); 
                    sz -= 2;
                }
                break;
        }
    }
    *ptr = 0;
    return out;
}


bool device_has_aspect(device_t *dev, int aspect) {
    for (int i = 0; i < dev->num_aspects; i++) {
        if (dev->aspects[i] == aspect) {
            return true;
        }
    }
    return false;
}


int device_count() {
    size_t count = 0;
    for (device_t *dev = device_get_all(); dev != NULL; dev = (device_t *) dev->_llitem.next) {
        count++;
    }
    return count;
}





CborError cbor_encode_deviceid(device_identifier_t *ser, uint8_t *out, size_t *outsz) {
    CborEncoder arrEnc, itemEnc;
    CborEncoder enc;
    CborError err;

    cbor_encoder_init(&enc, out, *outsz, 0);
    
    err = cbor_encoder_create_array(&enc, &arrEnc, ser->num_parts);
    if (err != CborNoError) {
        return err;
    }
    for (int i = 0; i < ser->num_parts; i++) {
        err = cbor_encoder_create_array(&arrEnc, &itemEnc, 2);
        if (err != CborNoError) {
            return err;
        }
        err = cbor_encode_uint(&itemEnc, ser->parts[i].type);
        if (err != CborNoError) {
            return err;
        }
        err = cbor_encode_byte_string(&itemEnc, ser->parts[i].data, ser->parts[i].len);
        if (err != CborNoError) {
            return err;
        }
        err = cbor_encoder_close_container(&arrEnc, &itemEnc);
        if (err != CborNoError) {
            return err;
        }

    }
    err = cbor_encoder_close_container(&enc, &arrEnc);
    if (err != CborNoError) {
        return err;
    }
    *outsz = enc.data.ptr - out;

    return CborNoError;
}

ccpeed_err_t deviceid_decode(device_identifier_t *serial, uint8_t *buf, size_t sz) {
    CborParser parser;
    CborValue val, arr, item;
    CborError err;
    uint32_t ival;

    err = cbor_parser_init(buf, sz, 0, &parser, &val);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }

    if (!cbor_value_is_array(&val)) {
        return CCPEED_ERROR_INVALID;
    }

    err = cbor_value_get_array_length(&val, &serial->num_parts);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }
    err = cbor_value_enter_container(&val, &arr);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }
    for (int i = 0; i < serial->num_parts; i++) {
        if (!cbor_value_is_array(&arr)) {
            return CCPEED_ERROR_INVALID;
        }
        err = cbor_value_enter_container(&arr, &item);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
        err = cbor_expect_uint32(&item, sizeof(device_id_type_names)/sizeof(device_id_type_names[0]), &ival);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
        serial->parts[i].type = ival;

        err = cbor_value_advance(&item);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
        if (!cbor_value_is_byte_string(&item)) {
            return CCPEED_ERROR_INVALID;
        }

        serial->parts[i].len = MAX_DEVICE_ID_DATALEN;
        err = cbor_value_copy_byte_string(&item, serial->parts[i].data, &serial->parts[i].len, NULL);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        err = cbor_value_advance(&item);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        err = cbor_value_leave_container(&arr, &item);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }
    err = cbor_value_leave_container(&val, &arr);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    return CCPEED_NO_ERR;
}

