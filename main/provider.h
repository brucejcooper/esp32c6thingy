#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <cbor.h>
#include "linked_list.h"
#include "ccpeed_err.h"

typedef struct provider_base_t {
    linked_list_item_t _llitem; // Must always be the first item in the struct.
    int type;
} provider_base_t;


void provider_init(provider_base_t *provider, int type);
void add_provider(provider_base_t *prov);
provider_base_t *get_all_providers();


#ifdef __cplusplus
}
#endif
