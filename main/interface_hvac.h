#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cbor.h>
#include "ast.h"
#include "ccpeed_err.h"

#define THINGIF_HVAC 5


#define THINGIF_HVAC_ATTR_TARGET_TEMPERATURE 1

#define THINGIF_HVAC_ATTR_SOURCE_SENSORS 2

#define THINGIF_HVAC_ATTR_MODE 3






typedef struct {

    bool is_target_temperature_present;
    int target_temperature;

    bool is_source_sensors_present;
    int source_sensors;

    bool is_mode_present;
    int mode;


} thingif_hvac_attr_t;


#define THINGIF_HVAC_ATTR_INIT { \
    .is_target_temperature_present = false, \
    .is_source_sensors_present = false, \
    .is_mode_present = false, \
} 


ccpeed_err_t thingif_hvac_attr_read(thingif_hvac_attr_t *attr, CborValue *val);
ccpeed_err_t thingif_hvac_attr_write(thingif_hvac_attr_t *attr, CborEncoder *enc);
void thingif_hvac_attr_free(thingif_hvac_attr_t *attr);
ccpeed_err_t thingif_hvac_op_call(uint32_t op, CborValue *params);