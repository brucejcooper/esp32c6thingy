
#include "interface_pushbutton.h"



ccpeed_err_t thingif_pushbutton_attr_read(thingif_pushbutton_attr_t *attr, CborValue *val) {
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

            case THINGIF_PUSHBUTTON_ATTR_CLICK_MAX_DURATION:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->click_max_duration));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_click_max_duration_present = true;
                break;

            case THINGIF_PUSHBUTTON_ATTR_LONGCLICK_DELAY:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->longclick_delay));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_longclick_delay_present = true;
                break;

            case THINGIF_PUSHBUTTON_ATTR_LONGCLICK_REPEAT_DELAY:

                if (!cbor_value_is_integer(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_int(&it, &(attr->longclick_repeat_delay));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_longclick_repeat_delay_present = true;
                break;

            case THINGIF_PUSHBUTTON_ATTR_ON_PRESS:

                err = ast_parse_from_cbor(&it, &(attr->on_press));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_press_present = true;
                break;

            case THINGIF_PUSHBUTTON_ATTR_ON_RELEASE:

                err = ast_parse_from_cbor(&it, &(attr->on_release));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_release_present = true;
                break;

            case THINGIF_PUSHBUTTON_ATTR_ON_CLICK:

                err = ast_parse_from_cbor(&it, &(attr->on_click));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_click_present = true;
                break;

            case THINGIF_PUSHBUTTON_ATTR_ON_LONG_PRESS:

                err = ast_parse_from_cbor(&it, &(attr->on_long_press));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_long_press_present = true;
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


ccpeed_err_t thingif_pushbutton_attr_write(thingif_pushbutton_attr_t *attr, CborEncoder *enc) {
    CborError err;
    CborEncoder mapEnc;
    size_t num_items = 0;


    if (attr->is_click_max_duration_present) {
        num_items++;
    }
    if (attr->is_longclick_delay_present) {
        num_items++;
    }
    if (attr->is_longclick_repeat_delay_present) {
        num_items++;
    }
    if (attr->is_on_press_present) {
        num_items++;
    }
    if (attr->is_on_release_present) {
        num_items++;
    }
    if (attr->is_on_click_present) {
        num_items++;
    }
    if (attr->is_on_long_press_present) {
        num_items++;
    }
    err = cbor_encoder_create_map(enc, &mapEnc, num_items);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    if (attr->is_click_max_duration_present) {
        err = cbor_encode_int(&mapEnc, attr->click_max_duration);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_longclick_delay_present) {
        err = cbor_encode_int(&mapEnc, attr->longclick_delay);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_longclick_repeat_delay_present) {
        err = cbor_encode_int(&mapEnc, attr->longclick_repeat_delay);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    if (attr->is_on_press_present) {
        err = ast_serialise_to_cbor(&(attr->on_press), &mapEnc);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }

    if (attr->is_on_release_present) {
        err = ast_serialise_to_cbor(&(attr->on_release), &mapEnc);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }

    if (attr->is_on_click_present) {
        err = ast_serialise_to_cbor(&(attr->on_click), &mapEnc);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

    }

    if (attr->is_on_long_press_present) {
        err = ast_serialise_to_cbor(&(attr->on_long_press), &mapEnc);
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

void thingif_pushbutton_attr_free(thingif_pushbutton_attr_t *attr) {
attr->is_click_max_duration_present = false;attr->is_longclick_delay_present = false;attr->is_longclick_repeat_delay_present = false;attr->is_on_press_present = false;
    ast_free(&(attr->on_press));attr->is_on_release_present = false;
    ast_free(&(attr->on_release));attr->is_on_click_present = false;
    ast_free(&(attr->on_click));attr->is_on_long_press_present = false;
    ast_free(&(attr->on_long_press));
} 

ccpeed_err_t thingif_pushbutton_op_call(uint32_t op, CborValue *params) {
    return CCPEED_ERROR_NOT_IMPLEMENTED;
}