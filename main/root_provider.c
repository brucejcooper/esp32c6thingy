
#include <stddef.h>
#include <stdint.h>
#include "provider.h"
#include "root_provider.h"
#include "dali_provider.h"
#include <esp_log.h>
#include <cbor.h>

#define TAG "root_provider"



ccpeed_err_t root_provider_init(root_provider_t *self, uint8_t *configBytes, size_t configLen) {
    provider_init(&self->super, ROOT_PROVIDER_ID);
    self->config = configBytes;
    self->configLen = configLen;

    // Now lets parse it
    CborValue it, item, providerConfig;
    CborParser parser;
    ccpeed_err_t cerr;

    CborError err = cbor_parser_init(self->config, configLen, 0, &parser, &it);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }

    if (!cbor_value_is_array(&it)) {
        return CCPEED_ERROR_INVALID;
    }

    size_t numProviders;
    err = cbor_value_get_array_length(&it, &numProviders);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }


    err = cbor_value_enter_container(&it, &item);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }
    for (int i = 0; i < numProviders; i++) {
        if (!cbor_value_is_array(&item)) {
            return CCPEED_ERROR_INVALID;
        }
        err = cbor_value_enter_container(&item, &providerConfig);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        if (!cbor_value_is_integer(&providerConfig)) {
            return CCPEED_ERROR_INVALID;
        }

        int providerId;
        err = cbor_value_get_int(&providerConfig, &providerId);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        err = cbor_value_advance(&providerConfig);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        switch (providerId) {
            case DALI_PROVIDER_ID:
                ESP_LOGI(TAG, "Initialising Dali Provider");
                dali_provider_t *newDaliProv = malloc(sizeof(dali_provider_t));
                if (!newDaliProv) {
                    return CCPEED_ERROR_NOMEM;
                }
                cerr = dali_provider_init(newDaliProv, &providerConfig);
                if (cerr) {
                    free(newDaliProv);
                    return cerr;
                }
                add_provider((provider_base_t *) newDaliProv);
                break;
            default:
                ESP_LOGW(TAG, "Invalid providerId %d", providerId);
                break;
        }

        err = cbor_value_leave_container(&item, &providerConfig);
        if (err != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }    
    }

    err = cbor_value_leave_container(&it, &item);
    if (err != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }

    return CCPEED_NO_ERR;

}

