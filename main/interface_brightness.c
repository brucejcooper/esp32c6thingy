
#include "interface_brightness.h"



ccpeed_err_t thingif_brightness_attr_read(thingif_brightness_attr_t *attr, CborValue *val) {
    CborValue it;
    size_t len, slen;
    CborError err;
    uint64_t key;

    if (!cbor_value_is_map(val)) {
        return CCPEED_ERROR_INVALID;
    }

    err = cbor_value_get_map_length(val, &len);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }

    
    cbor_value_enter_container(&it, val);
    while (len--) {
        if (!cbor_value_is_integer(&it)) {
            return CCPEED_ERROR_INVALID;
        }

        err = cbor_value_get_uint64(&it, &key);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
        err = cbor_value_advance(&it);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        switch (key) {

            case THINGIF_BRIGHTNESS_ATTR_LEVEL:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->level));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_level_present = true;
                break;

            case THINGIF_BRIGHTNESS_ATTR_MIN_LEVEL:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->min_level));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_min_level_present = true;
                break;

            case THINGIF_BRIGHTNESS_ATTR_MAX_LEVEL:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->max_level));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_max_level_present = true;
                break;

            case THINGIF_BRIGHTNESS_ATTR_POWER_ON_LEVEL:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->power_on_level));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_power_on_level_present = true;
                break;

            default:
                return CCPEED_ERROR_INVALID;
        }

        // Move to the next value in the map.
        err = cbor_value_advance(&it);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }
    err = cbor_value_leave_container(&it, val);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }
    return CCPEED_NO_ERR;
}


ccpeed_err_t thingif_brightness_attr_write(thingif_brightness_attr_t *attr, CborEncoder *enc) {
    CborError err;
    CborEncoder mapEnc;
    size_t num_items = 0;


    if (attr->is_level_present) {
        num_items++;
    }
    if (attr->is_min_level_present) {
        num_items++;
    }
    if (attr->is_max_level_present) {
        num_items++;
    }
    if (attr->is_power_on_level_present) {
        num_items++;
    }
    err = cbor_encoder_create_map(enc, &mapEnc, num_items);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    if (attr->is_level_present) {
        err = cbor_encode_int(&mapEnc, attr->level);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_min_level_present) {
        err = cbor_encode_int(&mapEnc, attr->min_level);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_max_level_present) {
        err = cbor_encode_int(&mapEnc, attr->max_level);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_power_on_level_present) {
        err = cbor_encode_int(&mapEnc, attr->power_on_level);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    err = cbor_encoder_close_container(enc, &mapEnc);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    return CCPEED_NO_ERR;
}

void thingif_brightness_attr_free(thingif_brightness_attr_t *attr) {
attr->is_level_present = false;attr->is_min_level_present = false;attr->is_max_level_present = false;attr->is_power_on_level_present = false;
} 

ccpeed_err_t thingif_brightness_op_call(uint32_t op, CborValue *params) {
    return CCPEED_ERROR_NOT_IMPLEMENTED;
}