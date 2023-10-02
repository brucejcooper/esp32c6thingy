
#include <stddef.h>
#include <stdint.h>
#include "provider.h"
#include "root_provider.h"
#include "dali_provider.h"
#include "cbor_helpers.h"
#include <esp_log.h>
#include <cbor.h>

#define TAG "root_provider"



ccpeed_err_t root_provider_init(root_provider_t *self) {
    provider_init(&self->super, ROOT_PROVIDER_ID, NULL, NULL, NULL);
    self->super.set_attr_fn = NULL;
    self->super.process_service_call_fn = NULL;
    self->super.encode_attributes_fn = NULL;
    return CCPEED_NO_ERR;

}

