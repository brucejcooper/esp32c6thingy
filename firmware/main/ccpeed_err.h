#pragma once

typedef enum {
    CCPEED_NO_ERR = 0,
    CCPEED_ERROR_INVALID,
    CCPEED_ERROR_NOMEM,
    CCPEED_ERROR_BUS_ERROR, // Some protocol error on a bus.
    CCPEED_ERROR_NOT_FOUND, 
    CCPEED_ERROR_NOT_IMPLEMENTED,
} ccpeed_err_t;