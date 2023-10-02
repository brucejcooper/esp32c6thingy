#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cbor.h>
#include "ast.h"
#include "ccpeed_err.h"

#define THINGIF_PUSHBUTTON 3


#define THINGIF_PUSHBUTTON_ATTR_CLICK_MAX_DURATION 1

#define THINGIF_PUSHBUTTON_ATTR_LONGCLICK_DELAY 2

#define THINGIF_PUSHBUTTON_ATTR_LONGCLICK_REPEAT_DELAY 3

#define THINGIF_PUSHBUTTON_ATTR_ON_PRESS 4

#define THINGIF_PUSHBUTTON_ATTR_ON_RELEASE 5

#define THINGIF_PUSHBUTTON_ATTR_ON_CLICK 6

#define THINGIF_PUSHBUTTON_ATTR_ON_LONG_PRESS 7






typedef struct {

    bool is_click_max_duration_present;
    int click_max_duration;

    bool is_longclick_delay_present;
    int longclick_delay;

    bool is_longclick_repeat_delay_present;
    int longclick_repeat_delay;

    bool is_on_press_present;
    ast_node_t on_press;

    bool is_on_release_present;
    ast_node_t on_release;

    bool is_on_click_present;
    ast_node_t on_click;

    bool is_on_long_press_present;
    ast_node_t on_long_press;


} thingif_pushbutton_attr_t;


#define THINGIF_PUSHBUTTON_ATTR_INIT { \
    .is_click_max_duration_present = false, \
    .is_longclick_delay_present = false, \
    .is_longclick_repeat_delay_present = false, \
    .is_on_press_present = false, \
    .is_on_release_present = false, \
    .is_on_click_present = false, \
    .is_on_long_press_present = false, \
} 


ccpeed_err_t thingif_pushbutton_attr_read(thingif_pushbutton_attr_t *attr, CborValue *val);
ccpeed_err_t thingif_pushbutton_attr_write(thingif_pushbutton_attr_t *attr, CborEncoder *enc);
void thingif_pushbutton_attr_free(thingif_pushbutton_attr_t *attr);
ccpeed_err_t thingif_pushbutton_op_call(uint32_t op, CborValue *params);