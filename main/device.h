#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "provider.h"
#include "linked_list.h"

#define MAX_ASPECTS 5

typedef struct {
    uint8_t serial[20];
    size_t len;
} device_serial_t;


typedef struct device_t {
    linked_list_item_t _llitem;
    provider_base_t *provider;
    device_serial_t serial;
    int aspects[MAX_ASPECTS];
    size_t num_aspects;
} device_t;


void add_device(device_t *dev);

void device_init(device_t *dev, provider_base_t *provider,  device_serial_t *serial, int *aspect, size_t num_apsects);

device_t *device_find_by_serial(device_serial_t *serial);
void device_delete(device_t *dev);
bool device_has_aspect(device_t *dev, int aspect);
device_t *device_get_all();
bool device_serial_equals(device_serial_t *s1, device_serial_t *s2);
char *device_serial_to_str(device_serial_t *serial, char *out);

#ifdef __cplusplus
}
#endif
