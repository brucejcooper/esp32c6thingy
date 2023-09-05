#include "cbor_helpers.h"


CborError cbor_value_get_uint32(CborValue *val, uint32_t *out) {
    uint64_t tmp;

    CborError err = cbor_value_get_uint64(val, &tmp);
    if (err == CborNoError) {
        *out = (uint32_t) tmp;
    }
    return err;
}

