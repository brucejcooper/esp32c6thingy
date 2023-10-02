#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cbor.h>
#include "ast.h"
#include "ccpeed_err.h"

#define THINGIF_TEMPERATURE 4


#define THINGIF_TEMPERATURE_ATTR_VALUE 1

#define THINGIF_TEMPERATURE_ATTR_POLL_FREQUENCY 2

#define THINGIF_TEMPERATURE_ATTR_ON_CHANGE 3

#define THINGIF_TEMPERATURE_ATTR_ALARM_HIGH_TRIPPED 4

#define THINGIF_TEMPERATURE_ATTR_ALARM_LOW_TRIPPED 5

#define THINGIF_TEMPERATURE_ATTR_ALARM_LOW 6

#define THINGIF_TEMPERATURE_ATTR_ALARM_HIGH 7

#define THINGIF_TEMPERATURE_ATTR_ON_ALARM_HIGH_TRIPPED 8

#define THINGIF_TEMPERATURE_ATTR_ON_ALARM_LOW_TRIPPED 9

#define THINGIF_TEMPERATURE_ATTR_ON_ALARM_HIGH_CLEARED 10

#define THINGIF_TEMPERATURE_ATTR_ON_ALARM_LOW_CLEARED 11






typedef struct {

    bool is_value_present;
    int value;

    bool is_poll_frequency_present;
    int poll_frequency;

    bool is_on_change_present;
    ast_node_t on_change;

    bool is_alarm_high_tripped_present;
    bool alarm_high_tripped;

    bool is_alarm_low_tripped_present;
    bool alarm_low_tripped;

    bool is_alarm_low_present;
    int alarm_low;

    bool is_alarm_high_present;
    int alarm_high;

    bool is_on_alarm_high_tripped_present;
    ast_node_t on_alarm_high_tripped;

    bool is_on_alarm_low_tripped_present;
    ast_node_t on_alarm_low_tripped;

    bool is_on_alarm_high_cleared_present;
    ast_node_t on_alarm_high_cleared;

    bool is_on_alarm_low_cleared_present;
    ast_node_t on_alarm_low_cleared;


} thingif_temperature_attr_t;


#define THINGIF_TEMPERATURE_ATTR_INIT { \
    .is_value_present = false, \
    .is_poll_frequency_present = false, \
    .is_on_change_present = false, \
    .is_alarm_high_tripped_present = false, \
    .is_alarm_low_tripped_present = false, \
    .is_alarm_low_present = false, \
    .is_alarm_high_present = false, \
    .is_on_alarm_high_tripped_present = false, \
    .is_on_alarm_low_tripped_present = false, \
    .is_on_alarm_high_cleared_present = false, \
    .is_on_alarm_low_cleared_present = false, \
} 


ccpeed_err_t thingif_temperature_attr_read(thingif_temperature_attr_t *attr, CborValue *val);
ccpeed_err_t thingif_temperature_attr_write(thingif_temperature_attr_t *attr, CborEncoder *enc);
void thingif_temperature_attr_free(thingif_temperature_attr_t *attr);
ccpeed_err_t thingif_temperature_op_call(uint32_t op, CborValue *params);