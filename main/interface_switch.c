
#include "interface_switch.h"



ccpeed_err_t thingif_switch_attr_read(thingif_switch_attr_t *attr, CborValue *val) {
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

            case THINGIF_SWITCH_ATTR_ON:

                if (!cbor_value_is_boolean(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_boolean(&it, &(attr->on));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_present = true;
                break;

            case THINGIF_SWITCH_ATTR_ON_CHANGE:

                err = ast_parse_from_cbor(&it, &(attr->on_change));
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_on_change_present = true;
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


ccpeed_err_t thingif_switch_attr_write(thingif_switch_attr_t *attr, CborEncoder *enc) {
    CborError err;
    CborEncoder mapEnc;
    size_t num_items = 0;


    if (attr->is_on_present) {
        num_items++;
    }
    if (attr->is_on_change_present) {
        num_items++;
    }
    err = cbor_encoder_create_map(enc, &mapEnc, num_items);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    if (attr->is_on_present) {
        err = cbor_encode_boolean(&mapEnc, attr->on);
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

    err = cbor_encoder_close_container(enc, &mapEnc);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    return CCPEED_NO_ERR;
}

void thingif_switch_attr_free(thingif_switch_attr_t *attr) {
attr->is_on_present = false;attr->is_on_change_present = false;
    ast_free(&(attr->on_change));
} 

ccpeed_err_t thingif_switch_op_call(uint32_t op, CborValue *params) {
    return CCPEED_ERROR_NOT_IMPLEMENTED;
}