#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cbor.h>
#include "ast.h"
#include "ccpeed_err.h"

#define THINGIF_DEVICE 0


#define THINGIF_DEVICE_ATTR_FW_VER 1




#define THINGIF_DEVICE_OP_UPGRADE_FW 1



typedef struct {

    bool is_fw_ver_present;
    char * fw_ver;


} thingif_device_attr_t;


#define THINGIF_DEVICE_ATTR_INIT { \
    .is_fw_ver_present = false, \
    .fw_ver = NULL, \
} 


ccpeed_err_t thingif_device_attr_read(thingif_device_attr_t *attr, CborValue *val);
ccpeed_err_t thingif_device_attr_write(thingif_device_attr_t *attr, CborEncoder *enc);
void thingif_device_attr_free(thingif_device_attr_t *attr);
ccpeed_err_t thingif_device_op_call(uint32_t op, CborValue *params);