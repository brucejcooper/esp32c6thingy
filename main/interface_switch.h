#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cbor.h>
#include "ast.h"
#include "ccpeed_err.h"

#define THINGIF_SWITCH 1


#define THINGIF_SWITCH_ATTR_ON 1

#define THINGIF_SWITCH_ATTR_ON_CHANGE 2




#define THINGIF_SWITCH_OP_TOGGLE_ON 1



typedef struct {

    bool is_on_present;
    bool on;

    bool is_on_change_present;
    ast_node_t on_change;


} thingif_switch_attr_t;


#define THINGIF_SWITCH_ATTR_INIT { \
    .is_on_present = false, \
    .is_on_change_present = false, \
} 


ccpeed_err_t thingif_switch_attr_read(thingif_switch_attr_t *attr, CborValue *val);
ccpeed_err_t thingif_switch_attr_write(thingif_switch_attr_t *attr, CborEncoder *enc);
void thingif_switch_attr_free(thingif_switch_attr_t *attr);
ccpeed_err_t thingif_switch_op_call(uint32_t op, CborValue *params);