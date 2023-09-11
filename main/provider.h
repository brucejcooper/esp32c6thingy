#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <cbor.h>
#include "linked_list.h"
#include "ccpeed_err.h"

// Forward reference to the device_t type. 
struct device_t;

typedef ccpeed_err_t (*device_set_attr_fn)(struct device_t *dev, int aspect_id, int attr_id, CborValue *val);
typedef ccpeed_err_t (*device_process_service_call_fn)(struct device_t *device, int aspectId, int serviceId, CborValue *params, size_t numParams);
typedef ccpeed_err_t (*device_encode_attributes_fn)(struct device_t *dev, int aspect_id, CborEncoder *encoder);


typedef struct provider_base_t {
    linked_list_item_t _llitem; // Must always be the first item in the struct.
    int type;

    device_set_attr_fn set_attr_fn;
    device_process_service_call_fn process_service_call_fn;
    device_encode_attributes_fn encode_attributes_fn;
} provider_base_t;


void provider_init(provider_base_t *provider, int type);
void add_provider(provider_base_t *prov);
provider_base_t *get_all_providers();


#ifdef __cplusplus
}
#endif
