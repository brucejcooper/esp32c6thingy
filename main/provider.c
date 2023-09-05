

#include "provider.h"
#include "linked_list.h"


static linked_list_t providers = {
    .head = NULL,
    .tail = NULL,
};


void add_provider(provider_base_t *provider) {
    ll_append(&providers, &provider->_llitem);
}

void provider_init(provider_base_t *provider, int type) {
    ll_init_item(&provider->_llitem);
    provider->type = type;
}


provider_base_t *get_all_providers() {
    return (provider_base_t *) providers.head;
}