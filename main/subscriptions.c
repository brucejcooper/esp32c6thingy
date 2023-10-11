
#include "ccpeed_err.h"
#include "subscriptions.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <cbor.h>
#include "cbor_helpers.h"

#define TAG "subscriptions"

#define MAX_SUBSCRIPTIONS 20
static subscription_t subscriptions[MAX_SUBSCRIPTIONS];
static size_t num_subscriptions = 0;


#define SUBSCRIPTION_ENCODING_NUM_FIELDS 5


static void subscription_log(esp_log_level_t level, subscription_t *sub, char *prefix) {
    char ipBuf[OT_IP6_ADDRESS_STRING_SIZE];
    char deviceBuf[100];

    otIp6AddressToString(&sub->subscriber_addr, ipBuf, sizeof(ipBuf));
    ESP_LOG_LEVEL(level, TAG, "%s%s dev %s iface %lu token '%.*s' mask 0x%lx", prefix, ipBuf, device_identifier_to_str(&sub->device_id, deviceBuf, sizeof(deviceBuf)), sub->interface_id, sub->token_len, sub->token, sub->attribute_mask);
}


static void dumpSubs() {
    ESP_LOGI(TAG, "Dumping %d subscritpions", num_subscriptions);
    for (subscription_t *sub = subscriptions; sub < (subscriptions + num_subscriptions); sub++) {
        subscription_log(ESP_LOG_INFO, sub, "- ");
    }
}

static ccpeed_err_t subscriptions_persist() {
    nvs_handle_t handle;
    esp_err_t err;
    CborEncoder enc, arrayEnc, itemEnc;
    uint8_t buf[1000];
    uint8_t buf2[100];
    size_t sz;
    CborError cborErr;

    dumpSubs();
    cbor_encoder_init(&enc, buf, sizeof(buf), 0);

    cborErr = cbor_encoder_create_array(&enc, &arrayEnc, num_subscriptions);
    if (cborErr != CborNoError) {
        ESP_LOGW(TAG, "Error persisiting - Could not create array");
        return CCPEED_ERROR_NOMEM;
    }
    subscription_t *sub = subscriptions;
    for (int i = 0; i < num_subscriptions; i++) {
        cborErr = cbor_encoder_create_array(&arrayEnc, &itemEnc, SUBSCRIPTION_ENCODING_NUM_FIELDS);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting - Could not create item array");
            return CCPEED_ERROR_NOMEM;
        }

        cborErr = cbor_encode_byte_string(&itemEnc, sub->subscriber_addr.mFields.m8, OT_IP6_ADDRESS_SIZE);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting item %d - Could not persist IP address", i);
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_encode_byte_string(&itemEnc, sub->token, sub->token_len);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting item %d - Could not persist token", i);
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_encode_deviceid(&sub->device_id, buf2, &sz);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting item %d - Could not encode deviceId", i);
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_encode_byte_string(&itemEnc, buf2, sz);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting item %d - Could not encode deviceID buf", i);
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_encode_int(&itemEnc, sub->interface_id);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting item %d - Could not encode interface ID", i);
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_encode_int(&itemEnc, sub->attribute_mask);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting item %d - Could not encode attribute mask", i);
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_encoder_close_container(&arrayEnc, &itemEnc);
        if (cborErr != CborNoError) {
            ESP_LOGW(TAG, "Error persisiting item %d - Could not close container: %s", i,  cbor_error_string(cborErr));
            return CCPEED_ERROR_NOMEM;
        }
    }

    cborErr = cbor_encoder_close_container(&enc, &arrayEnc);
    if (cborErr != CborNoError) {
        ESP_LOGW(TAG, "Error persisiting- Could not close outside container");
        return CCPEED_ERROR_INVALID;
    }


    sz = enc.data.ptr-buf;
    ESP_LOGI(TAG, "Subscriptions encoded to %d bytes", sz);

    err = nvs_open("subs", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Could not open NVS storage");
        return CCPEED_ERROR_NOMEM;
    }
    err = nvs_set_blob(handle, "subs", buf, sz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error persisiting- Could save blob");
        return CCPEED_ERROR_INVALID;
    }
    nvs_close(handle);

    return CCPEED_NO_ERR;
}

ccpeed_err_t subscriptions_read() {
    CborValue val;
    CborValue arrayVal;
    CborValue itemVal;
    CborParser parser;

    nvs_handle_t handle;
    esp_err_t err;
    // Before we can parse values, we must read the buffer into memory.  Assume that every item is only max 20 bytes bigger than its decoded val.
    uint8_t buf[MAX_SUBSCRIPTIONS*(sizeof(subscription_t) + 20) + 20];
    uint8_t buf2[100];
    size_t sz;
    CborError cborErr;
    size_t new_num_subs, nitem;
    // We need a complete second copy of the subscriptons, so  that we can parse them all before messing
    // with the real values. 
    subscription_t new_subs[MAX_SUBSCRIPTIONS];
    subscription_t *sub;

    new_num_subs = 0;

    err = nvs_open("subs", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // The namespace doesn't exist yet... That is fine, it means we've never persisted the subscriptions before.
        num_subscriptions = 0;
        return CCPEED_NO_ERR;
    }
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Could not open NVS storage: %s", esp_err_to_name(err));
        return CCPEED_ERROR_NOMEM;
    }
    err = nvs_get_blob(handle, "subs", buf, &sz);
    if (err == ESP_ERR_NOT_FOUND) {
        // We've created the partition, but never saved the values... odd but valid
        num_subscriptions = 0;
        return CCPEED_NO_ERR;
    }
    if (err != ESP_OK) {
        return CCPEED_ERROR_INVALID;
    }
    nvs_close(handle);

    cborErr = cbor_parser_init(buf, sz, 0, &parser, &val);
    if (cborErr != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }

    if (!cbor_value_is_array(&val)) {
        return CCPEED_ERROR_INVALID;
    }
    cborErr = cbor_value_get_array_length(&val, &new_num_subs);
    if (cborErr != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }
    if (new_num_subs > MAX_SUBSCRIPTIONS) {
        return CCPEED_ERROR_NOMEM;
    }

    cborErr = cbor_value_enter_container(&val, &arrayVal);
    if (cborErr != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }
    sub = new_subs;
    for (int i = 0; i < new_num_subs; i++, sub++) {
        if (!cbor_value_is_array(&val)) {
            return CCPEED_ERROR_INVALID;
        }
        cborErr = cbor_value_get_array_length(&val, &nitem);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
        if (nitem != SUBSCRIPTION_ENCODING_NUM_FIELDS) {
            return CCPEED_ERROR_INVALID;
        }

        cborErr = cbor_value_enter_container(&arrayVal, &itemVal);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        // IP Address
        if (!cbor_value_is_byte_string(&itemVal)) {
            return CCPEED_ERROR_INVALID;
        }
        cborErr = cbor_value_get_string_length(&itemVal, &sz);
        if (cborErr != CborNoError || sz != OT_IP6_ADDRESS_SIZE) {
            return CCPEED_ERROR_INVALID;
        }
        
        cborErr = cbor_value_copy_byte_string(&itemVal, sub->subscriber_addr.mFields.m8, &sz, &itemVal);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_NOMEM;
        }

        //  Token
        if (!cbor_value_is_byte_string(&itemVal)) {
            return CCPEED_ERROR_INVALID;
        }
        cborErr = cbor_value_get_string_length(&itemVal, &sub->token_len);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
        if (sub->token_len > 8) {
            return CCPEED_ERROR_INVALID;
        }

        cborErr = cbor_value_copy_byte_string(&itemVal, sub->token, &sub->token_len, &itemVal);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_NOMEM;
        }

        // DeviceID.
        if (!cbor_value_is_byte_string(&itemVal)) {
            return CCPEED_ERROR_INVALID;
        }
        sz = sizeof(buf2);
        cborErr = cbor_value_copy_byte_string(&itemVal, buf2, &sz, &itemVal);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_NOMEM;
        }
        err = deviceid_decode(&sub->device_id, buf2, sz);
        if (err != CCPEED_NO_ERR) {
            return err;
        }

        // InterfaceID
        cborErr = cbor_expect_uint32(&itemVal, UINT32_MAX, &sub->interface_id);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_value_advance(&itemVal);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        // Attribute Mask
        cborErr = cbor_expect_uint32(&itemVal, UINT32_MAX, &sub->attribute_mask);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_NOMEM;
        }
        cborErr = cbor_value_advance(&itemVal);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }

        cborErr = cbor_value_leave_container(&arrayVal, &itemVal);
        if (cborErr != CborNoError) {
            return CCPEED_ERROR_INVALID;
        }
    }

    cborErr = cbor_value_leave_container(&val, &arrayVal);
    if (cborErr != CborNoError) {
        return CCPEED_ERROR_INVALID;
    }

    // Parsing succeeded completely, so we can copy into the real array.
    memcpy(subscriptions, new_subs, new_num_subs*sizeof(subscription_t));
    num_subscriptions = new_num_subs;
    return CCPEED_NO_ERR;
}



ccpeed_err_t subscription_append(device_identifier_t *device_id, uint32_t interface_id, const uint8_t *token, size_t token_sz, const otMessageInfo *msgInfo, uint32_t attribute_mask) {
    if (num_subscriptions == MAX_SUBSCRIPTIONS) {
        return CCPEED_ERROR_NOMEM;
    }
    
    memcpy(&subscriptions[num_subscriptions].subscriber_addr, &msgInfo->mPeerAddr, sizeof(otIp6Address));
    memcpy(subscriptions[num_subscriptions].token, token, token_sz);
    subscriptions[num_subscriptions].token_len = token_sz;
    memcpy(&subscriptions[num_subscriptions].device_id, device_id, sizeof(device_identifier_t));
    subscriptions[num_subscriptions].interface_id = interface_id;
    subscriptions[num_subscriptions].attribute_mask = attribute_mask;

    num_subscriptions++;
    return subscriptions_persist();
}

subscription_t *subscription_find(device_identifier_t *device_id, uint32_t interface_id, const uint8_t *token, size_t token_sz, const otMessageInfo *msgInfo) {
    for (subscription_t *sub = subscriptions; sub < subscriptions + num_subscriptions; sub++) {
        if (otIp6IsAddressEqual(&msgInfo->mPeerAddr, &sub->subscriber_addr) &&
            device_identifier_equals(device_id, &sub->device_id) &&
            interface_id == sub->interface_id &&
            token_sz == sub->token_len &&
            memcmp(token, sub->token, token_sz) == 0
        ) {
            return sub;
        }
    }
    return NULL;
}

ccpeed_err_t subscription_delete(subscription_t *sub) {
    int numAbove  = num_subscriptions - (sub - subscriptions) - 1;
    memcpy(sub, sub+1, numAbove*sizeof(subscription_t));
    num_subscriptions--;

    return subscriptions_persist();
}
