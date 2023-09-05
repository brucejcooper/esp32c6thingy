#pragma once

#include <stdint.h>
#include <cbor.h>

CborError cbor_value_get_uint32(CborValue *val, uint32_t *out);
