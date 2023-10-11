

#include "provider.h"
#include "linked_list.h"


static linked_list_t providers = {
    .head = NULL,
};


void add_provider(provider_base_t *provider) {
    ll_append(&providers, &provider->_llitem);
}

void provider_init(provider_base_t *provider, int type, device_encode_attributes_fn getter, device_set_attr_fn setter, device_process_service_call_fn servicefn) {
    ll_init_item(&provider->_llitem);
    provider->type = type;
    provider->set_attr_fn = setter;
    provider->process_service_call_fn = servicefn;
    provider->encode_attributes_fn = getter;
}


provider_base_t *get_all_providers() {
    return (provider_base_t *) providers.head;
}
