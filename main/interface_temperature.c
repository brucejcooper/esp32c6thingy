
#include "interface_temperature.h"



ccpeed_err_t thingif_temperature_attr_read(thingif_temperature_attr_t *attr, CborValue *val) {
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

            case THINGIF_TEMPERATURE_ATTR_VALUE:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->value));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_value_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_POLL_FREQUENCY:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->poll_frequency));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_poll_frequency_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ON_CHANGE:

                err = ast_parse_from_cbor(&it, &(attr->on_change));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_change_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ALARM_HIGH_TRIPPED:

                if (!cbor_value_is_boolean(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_boolean(&it, &(attr->alarm_high_tripped));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_alarm_high_tripped_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ALARM_LOW_TRIPPED:

                if (!cbor_value_is_boolean(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_boolean(&it, &(attr->alarm_low_tripped));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_alarm_low_tripped_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ALARM_LOW:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->alarm_low));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_alarm_low_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ALARM_HIGH:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->alarm_high));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_alarm_high_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ON_ALARM_HIGH_TRIPPED:

                err = ast_parse_from_cbor(&it, &(attr->on_alarm_high_tripped));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_alarm_high_tripped_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ON_ALARM_LOW_TRIPPED:

                err = ast_parse_from_cbor(&it, &(attr->on_alarm_low_tripped));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_alarm_low_tripped_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ON_ALARM_HIGH_CLEARED:

                err = ast_parse_from_cbor(&it, &(attr->on_alarm_high_cleared));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_alarm_high_cleared_present = true;
                break;

            case THINGIF_TEMPERATURE_ATTR_ON_ALARM_LOW_CLEARED:

                err = ast_parse_from_cbor(&it, &(attr->on_alarm_low_cleared));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_alarm_low_cleared_present = true;
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


ccpeed_err_t thingif_temperature_attr_write(thingif_temperature_attr_t *attr, CborEncoder *enc) {
    CborError err;
    CborEncoder mapEnc;
    size_t num_items = 0;


    if (attr->is_value_present) {
        num_items++;
    }
    if (attr->is_poll_frequency_present) {
        num_items++;
    }
    if (attr->is_on_change_present) {
        num_items++;
    }
    if (attr->is_alarm_high_tripped_present) {
        num_items++;
    }
    if (attr->is_alarm_low_tripped_present) {
        num_items++;
    }
    if (attr->is_alarm_low_present) {
        num_items++;
    }
    if (attr->is_alarm_high_present) {
        num_items++;
    }
    if (attr->is_on_alarm_high_tripped_present) {
        num_items++;
    }
    if (attr->is_on_alarm_low_tripped_present) {
        num_items++;
    }
    if (attr->is_on_alarm_high_cleared_present) {
        num_items++;
    }
    if (attr->is_on_alarm_low_cleared_present) {
        num_items++;
    }
    err = cbor_encoder_create_map(enc, &mapEnc, num_items);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    if (attr->is_value_present) {
        err = cbor_encode_int(&mapEnc, attr->value);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_poll_frequency_present) {
        err = cbor_encode_int(&mapEnc, attr->poll_frequency);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_on_change_present) {
        err = ast_serialise_to_cbor(&(attr->on_change), &mapEnc);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }

    if (attr->is_alarm_high_tripped_present) {
        err = cbor_encode_boolean(&mapEnc, attr->alarm_high_tripped);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_alarm_low_tripped_present) {
        err = cbor_encode_boolean(&mapEnc, attr->alarm_low_tripped);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_alarm_low_present) {
        err = cbor_encode_int(&mapEnc, attr->alarm_low);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_alarm_high_present) {
        err = cbor_encode_int(&mapEnc, attr->alarm_high);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_on_alarm_high_tripped_present) {
        err = ast_serialise_to_cbor(&(attr->on_alarm_high_tripped), &mapEnc);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }

    if (attr->is_on_alarm_low_tripped_present) {
        err = ast_serialise_to_cbor(&(attr->on_alarm_low_tripped), &mapEnc);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }

    if (attr->is_on_alarm_high_cleared_present) {
        err = ast_serialise_to_cbor(&(attr->on_alarm_high_cleared), &mapEnc);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }

    if (attr->is_on_alarm_low_cleared_present) {
        err = ast_serialise_to_cbor(&(attr->on_alarm_low_cleared), &mapEnc);
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

void thingif_temperature_attr_free(thingif_temperature_attr_t *attr) {
attr->is_value_present = false;attr->is_poll_frequency_present = false;attr->is_on_change_present = false;
    ast_free(&(attr->on_change));attr->is_alarm_high_tripped_present = false;attr->is_alarm_low_tripped_present = false;attr->is_alarm_low_present = false;attr->is_alarm_high_present = false;attr->is_on_alarm_high_tripped_present = false;
    ast_free(&(attr->on_alarm_high_tripped));attr->is_on_alarm_low_tripped_present = false;
    ast_free(&(attr->on_alarm_low_tripped));attr->is_on_alarm_high_cleared_present = false;
    ast_free(&(attr->on_alarm_high_cleared));attr->is_on_alarm_low_cleared_present = false;
    ast_free(&(attr->on_alarm_low_cleared));
} 

ccpeed_err_t thingif_temperature_op_call(uint32_t op, CborValue *params) {
    return CCPEED_ERROR_NOT_IMPLEMENTED;
}