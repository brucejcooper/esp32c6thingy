#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cbor.h>
#include "ast.h"
#include "ccpeed_err.h"

#define THINGIF_BRIGHTNESS 2


#define THINGIF_BRIGHTNESS_ATTR_LEVEL 1

#define THINGIF_BRIGHTNESS_ATTR_MIN_LEVEL 2

#define THINGIF_BRIGHTNESS_ATTR_MAX_LEVEL 3

#define THINGIF_BRIGHTNESS_ATTR_POWER_ON_LEVEL 4




#define THINGIF_BRIGHTNESS_OP_DELTA 1

#define THINGIF_BRIGHTNESS_OP_RECALL_MAX_LEVEL 2



typedef struct {

    bool is_level_present;
    int level;

    bool is_min_level_present;
    int min_level;

    bool is_max_level_present;
    int max_level;

    bool is_power_on_level_present;
    int power_on_level;


} thingif_brightness_attr_t;


#define THINGIF_BRIGHTNESS_ATTR_INIT { \
    .is_level_present = false, \
    .is_min_level_present = false, \
    .is_max_level_present = false, \
    .is_power_on_level_present = false, \
} 


ccpeed_err_t thingif_brightness_attr_read(thingif_brightness_attr_t *attr, CborValue *val);
ccpeed_err_t thingif_brightness_attr_write(thingif_brightness_attr_t *attr, CborEncoder *enc);
void thingif_brightness_attr_free(thingif_brightness_attr_t *attr);
ccpeed_err_t thingif_brightness_op_call(uint32_t op, CborValue *params);