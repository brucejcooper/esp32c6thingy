
#include "interface_device.h"



ccpeed_err_t thingif_device_attr_read(thingif_device_attr_t *attr, CborValue *val) {
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

            case THINGIF_DEVICE_ATTR_FW_VER:

                if (!cbor_value_is_text_string(&it)) {
                    return CCPEED_ERROR_INVALID;
                }
                err = cbor_value_get_string_length(&it, &slen);
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }
                attr->fw_ver = malloc(slen);
                if (!attr->fw_ver) {
                    return CCPEED_ERROR_NOMEM;
                }

                err = cbor_value_copy_text_string(&it, attr->fw_ver, slen, NULL);
                if (err != CborNoError) {
                    return CCPEED_ERROR_INVALID;
                }

                attr->is_fw_ver_present = true;
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


ccpeed_err_t thingif_device_attr_write(thingif_device_attr_t *attr, CborEncoder *enc) {
    CborError err;
    CborEncoder mapEnc;
    size_t num_items = 0;


    if (attr->is_fw_ver_present) {
        num_items++;
    }
    err = cbor_encoder_create_map(enc, &mapEnc, num_items);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    if (attr->is_fw_ver_present) {
        err = cbor_encode_text_stringz(&mapEnc, attr->fw_ver);
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

void thingif_device_attr_free(thingif_device_attr_t *attr) {
attr->is_fw_ver_present = false;
    free(attr->fw_ver);
} 

ccpeed_err_t thingif_device_op_call(uint32_t op, CborValue *params) {
    return CCPEED_ERROR_NOT_IMPLEMENTED;
}