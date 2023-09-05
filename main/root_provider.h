#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "provider.h"
#include <cbor.h>
#include "ccpeed_err.h"


#define ROOT_PROVIDER_ID 0



typedef struct {
    provider_base_t super;
    uint8_t *config;
    size_t configLen;
} root_provider_t;


ccpeed_err_t root_provider_init(root_provider_t *self, uint8_t *configBytes, size_t configLen);

#ifdef __cplusplus
}
#endif
