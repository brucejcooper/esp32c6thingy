#pragma once

#include <stdint.h>
#include <cbor.h>

CborError cbor_expect_uint32(CborValue *val, const uint32_t max_value, uint32_t *out);
CborError cbor_expect_uint16(CborValue *val, const uint16_t max_value, uint16_t *out);
CborError cbor_expect_bool(CborValue *val, bool *out);
