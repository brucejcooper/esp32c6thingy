#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "provider.h"
#include "linked_list.h"

#define MAX_ASPECTS 5

typedef enum {
    DEVID_TYPE_INDEX = 0, // The least specific - Try using _anything_ else. 
    DEVID_TYPE_GPIOPIN = 1,
    DEVID_TYPE_SERIAL_NUMBER = 2,
    DEVID_TYPE_MAC = 3,
    DEVID_TYPE_GTIN = 4, // https://www.gs1.org/standards/id-keys/gtin
} ccpeed_device_identifier_part_type_t;


#define MAX_DEVICE_ID_DATALEN 16
#define MAX_DEVICE_ID_DEPTH 2

typedef struct {
    ccpeed_device_identifier_part_type_t type;
    uint8_t data[MAX_DEVICE_ID_DATALEN];
    size_t len;
} ccpeed_device_identifier_part_t;


typedef struct {
    size_t num_parts;
    ccpeed_device_identifier_part_t parts[MAX_DEVICE_ID_DEPTH];
} device_identifier_t;


typedef struct device_t {
    linked_list_item_t _llitem;
    provider_base_t *provider;
    device_identifier_t id;
    int aspects[MAX_ASPECTS];
    size_t num_aspects;
} device_t;


void add_device(device_t *dev);

void device_init(device_t *dev, provider_base_t *provider,  device_identifier_t *id, int *aspects, size_t num_apsects);

device_t *device_find_by_id(device_identifier_t *id);
void device_delete(device_t *dev);
bool device_has_aspect(device_t *dev, int aspect);
device_t *device_get_all();
bool device_identifier_equals(device_identifier_t *s1, device_identifier_t *s2);
char *device_identifier_to_str(device_identifier_t *id, char *out, size_t sz);
int device_count();

CborError cbor_encode_deviceid(device_identifier_t *ser, uint8_t *out, size_t *outsz);
ccpeed_err_t deviceid_decode(device_identifier_t *id, uint8_t *buf, size_t sz);

#ifdef __cplusplus
}
#endif
