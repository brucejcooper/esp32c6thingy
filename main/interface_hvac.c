
#include "interface_hvac.h"



ccpeed_err_t thingif_hvac_attr_read(thingif_hvac_attr_t *attr, CborValue *val) {
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

            case THINGIF_HVAC_ATTR_TARGET_TEMPERATURE:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->target_temperature));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_target_temperature_present = true;
                break;

            case THINGIF_HVAC_ATTR_SOURCE_SENSORS:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->source_sensors));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_source_sensors_present = true;
                break;

            case THINGIF_HVAC_ATTR_MODE:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->mode));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_mode_present = true;
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


ccpeed_err_t thingif_hvac_attr_write(thingif_hvac_attr_t *attr, CborEncoder *enc) {
    CborError err;
    CborEncoder mapEnc;
    size_t num_items = 0;


    if (attr->is_target_temperature_present) {
        num_items++;
    }
    if (attr->is_source_sensors_present) {
        num_items++;
    }
    if (attr->is_mode_present) {
        num_items++;
    }
    err = cbor_encoder_create_map(enc, &mapEnc, num_items);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    if (attr->is_target_temperature_present) {
        err = cbor_encode_int(&mapEnc, attr->target_temperature);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_source_sensors_present) {
        err = cbor_encode_int(&mapEnc, attr->source_sensors);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_mode_present) {
        err = cbor_encode_int(&mapEnc, attr->mode);
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

void thingif_hvac_attr_free(thingif_hvac_attr_t *attr) {
attr->is_target_temperature_present = false;attr->is_source_sensors_present = false;attr->is_mode_present = false;
} 

ccpeed_err_t thingif_hvac_op_call(uint32_t op, CborValue *params) {
    return CCPEED_ERROR_NOT_IMPLEMENTED;
}