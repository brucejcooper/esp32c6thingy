#include <stddef.h>
#include <stdint.h>
#include "device.h"

#define TAG "device"

static linked_list_t devices = {
    .head = NULL,
    .tail = NULL,
};


device_t *device_get_all() {
    return (device_t *) devices.head;
}


void add_device(device_t *dev) {
    ll_append(&devices, &dev->_llitem);
}


void device_init(device_t *dev, provider_base_t *provider, device_serial_t *serial, int *aspects, size_t num_apsects) {
    ll_init_item(&dev->_llitem);
    dev->provider = provider;
    memcpy(&(dev->serial), serial, sizeof(device_serial_t));
    memcpy(dev->aspects, aspects, num_apsects*sizeof(int));
    dev->num_aspects = num_apsects;
}

/**
 * @brief Determines if two serial numbers are equal.
 * 
 * @param s1 
 * @param s2 
 * @return true 
 * @return false 
 */
bool device_serial_equals(device_serial_t *s1, device_serial_t *s2) {
    return s1->len == s2->len && memcmp(s1->serial, s2->serial, s1->len) == 0;
}


device_t *device_find_by_serial(device_serial_t *serial) {
    for (device_t *d = (device_t *) devices.head; d != NULL; d = (device_t *) d->_llitem.next) {
        if (device_serial_equals(serial, &d->serial)) {
            return d;
        }
    }
    return NULL;
}

/**
 * @brief Destructor for devices. Removes the device from the "all_devices" list, and frees its memory. 
 * 
 * @param dev 
 */
void device_delete(device_t *dev) {
    ll_remove(&devices, dev);
    free(dev);
}

static inline char hexToChar(unsigned int val) {
    assert(val < 16);
    if (val < 10) {
        return '0' + val;
    } else {
        return 'A' - 10 + val;
    } 
}


char *device_serial_to_str(device_serial_t *serial, char *out) {
    char *ptr = out;
    uint8_t *bufptr = serial->serial;
    for (int i = 0; i < serial->len; i++) {
        *ptr++ = hexToChar((*bufptr) >> 4); 
        *ptr++ = hexToChar((*bufptr++) & 0x0F); 
    }
    *ptr = 0;
    return out;
}


bool device_has_aspect(device_t *dev, int aspect) {
    for (int i = 0; i < dev->num_aspects; i++) {
        if (dev->aspects[i] == aspect) {
            return true;
        }
    }
    return false;
}


int device_count() {
    size_t count = 0;
    for (device_t *dev = device_get_all(); dev != NULL; dev = (device_t *) dev->_llitem.next) {
        count++;
    }
    return count;
}