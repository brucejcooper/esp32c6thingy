#include "cbor_helpers.h"


CborError cbor_expect_uint32(CborValue *val, const uint32_t max_value, uint32_t *out) {
    uint64_t tmp;

    if (!cbor_value_is_unsigned_integer(val)) {
        return CborErrorIllegalType;
    }

    CborError err = cbor_value_get_uint64(val, &tmp);
    if (err == CborNoError) {
        if (tmp <= max_value) {
            *out = (uint32_t) tmp;
        } else {
            return CborErrorIllegalNumber;
        }
    }
    return err;
}

CborError cbor_expect_bool(CborValue *val, bool *out) {
    bool tmp;

    if (!cbor_value_is_boolean(val)) {
        return CborErrorIllegalType;
    }

    CborError err = cbor_expect_bool(val, &tmp);
    if (err == CborNoError) {
        *out = tmp;
    }
    return err;
}

