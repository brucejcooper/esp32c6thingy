#include <cbor.h>


typedef struct {
    void **items;
    size_t num_items;
    size_t capacity;
} vector_t;

typedef enum {
    EXPRESSION_SEQUENCE = 0,
    EXPRESSION_TEST = 1,
} expression_id_t;

typedef enum {
    EXPRESSION_TYPE_UINT32,
    EXPRESSION_TYPE_INT32,
    EXPRESSION_TYPE_FLOAT32,
    EXPRESSION_TYPE_BOOL,
    EXPRESSION_TYPE_VECTOR,
    EXPRESSION_TYPE_MAP,
} expression_value_type_t;

typedef struct {
    expression_value_type_t type;
    union {
        bool boolVal;
        uint32_t uintVal;
        int32_t intVal;
        float floatVal;
        vector_t *vector;
    };
} expression_value_t;


CborError evaluate_cbor_expression(CborValue *expr, CborValue *param, CborEncoder *out) {
    CborTag tag;
    CborError err;
    CborValue expression;
    uint64_t exprType;
    size_t num_params;
    uint8_t buf[256];
    size_t bufsz;
    CborEncoder subexpr_encoder;



    switch (cbor_value_get_type(expr)) {
        case CborTagType:
            err = cbor_value_get_tag(expr, &tag);
            if (err != CborNoError) {
                return err;
            }
            if (tag == 20) {
                // Items with semantic tag of 20 are considered an expression, and must be followed by an array
                err = cbor_value_advance(expr);
                if (err != CborNoError) {
                    return err;
                }
                if (!cbor_value_is_array(expr)) {
                    return CborErrorInappropriateTagForType;
                }
                err = cbor_value_get_array_length(expr, &num_params);
                if (err != CborNoError) {
                    return err;
                }
                if (num_params < 1) {
                    return CborErrorImproperValue;
                }
                err = cbor_value_enter_container(expr, &expression);
                if (err != CborNoError) {
                    return err;
                }
                if (!cbor_value_is_unsigned_integer(&expression)) {
                    return CborErrorImproperValue;
                }
                err = cbor_value_get_uint64(&expression, &exprType);
                if (err != CborNoError) {
                    return err;
                }
                num_params--; // Consume the first value of the array.
                err = cbor_value_advance(&expression);
                if (err != CborNoError) {
                    return err;
                }
                switch (exprType) {
                    case EXPRESSION_SEQUENCE:
                        bufsz = 0;
                        // Execute each sub-expression in sequence, returning the last one. 
                        while (num_params--) {
                            cbor_encoder_init(&subexpr_encoder, buf, sizeof(buf), 0);
                            err = evaluate_cbor_expression(&expression, param, &subexpr_encoder);
                            if (err != CborNoError) {
                                return err;
                            }
                            err = cbor_value_advance(&expression);
                            if (err != CborNoError) {
                                return err;
                            }
                            bufsz = subexpr_encoder.data.ptr - buf;
                            num_params--;
                        }
                        break;
                    case EXPRESSION_TEST:
                        // Exeucte the next value, and test its value.
                        cbor_encoder_init(&subexpr_encoder, buf, sizeof(buf), 0);
                        err = evaluate_cbor_expression(&expression, param, &subexpr_encoder);
                        if (err != CborNoError) {
                            return err;
                        }
                        err = cbor_value_advance(&expression);
                        if (err != CborNoError) {
                            return err;
                        }
                        bufsz = subexpr_encoder.data.ptr - buf;
                        break;
                    default:
                        break;
                }

                err = cbor_value_leave_container(expr, &expression);
                if (err != CborNoError) {
                    return err;
                }
            } else {
                cbor_encode_tag(out, tag);
            }

            break;
        case CborIntegerType:
        case CborByteStringType:
        case CborTextStringType:
        case CborArrayType:
        case CborMapType:
        case CborSimpleType:
        case CborBooleanType:
        case CborNullType:
        case CborUndefinedType:
        case CborHalfFloatType:
        case CborFloatType:
        case CborDoubleType:
        default:
            // We have covered all other options above, so this is bad.

    }
}