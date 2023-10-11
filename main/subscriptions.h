
#include "openthread/ip6.h"
#include "device.h"

typedef struct {
    otIp6Address subscriber_addr;
    uint8_t token[8];
    size_t token_len;
    device_identifier_t device_id;
    uint32_t interface_id;
    /**
     * Only respond to changes in the attributes that match this mask. Each bit represents its corresponding attribute ID.
     * If this is 0 then all attributes will match.
     */
    uint32_t attribute_mask; 
} subscription_t;


ccpeed_err_t subscription_append(device_identifier_t *device, uint32_t interface_id, const uint8_t *token, size_t token_sz, const otMessageInfo *msgInfo, uint32_t attribute_mask);
subscription_t *subscription_find(device_identifier_t *device_id, uint32_t interface_id, const uint8_t *token, size_t token_sz, const otMessageInfo *msgInfo);
ccpeed_err_t subscription_delete(subscription_t *sub);
ccpeed_err_t subscriptions_read();
