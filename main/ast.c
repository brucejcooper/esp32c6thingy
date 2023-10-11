#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include "ast.h"
#include "device.h"
#include "interface_switch.h"
#include "interface_brightness.h"




/**
 Lookup of values for each of the ast_operator_type_t types up until LITERAL.  All subsequent values have got different amounts. 
*/
static const size_t expected_children[] = {
    3,
    2,
    2,
    2,
    2,
    2,
    2,
    1,
    1,
    2
};

static const char *TAG = "ast";

static const ast_literal_t null_ast = {
    .type = AST_LITERAL_NULL,
    .intVal = 0,
};


bool ast_stack_alloc(ast_stack_t *stack, size_t sz) {
    stack->base = malloc(sz*sizeof(ast_literal_t));
    if (!stack->base) {
        return false;
    }
    stack->capacity = sz;
    stack->ptr = stack->base;

    return true;
}

void ast_stack_free(ast_stack_t *stack) {
    free(stack->base);
    stack->base = stack->ptr = NULL;
}

static void ast_stack_push_intlike(ast_literal_type_t type, int32_t val, ast_stack_t *stack) {
    assert(stack->ptr - stack->base < stack->capacity);
    stack->ptr->type = type;
    stack->ptr->intVal = val;
    stack->ptr++;
}

static void ast_stack_push(ast_literal_t *literal, ast_stack_t *stack) {
    assert(stack->ptr - stack->base < stack->capacity);
    memcpy(stack->ptr, literal, sizeof(ast_literal_t));
    stack->ptr++;
}

static void ast_stack_pop(ast_literal_t *val, ast_stack_t *stack) {
    assert (stack->ptr > stack->base);
    memcpy(val, stack->ptr, sizeof(ast_literal_t));
    stack->ptr--;
}

static int32_t ast_stack_popint(ast_stack_t *stack) {
    assert(stack->ptr > stack->base);
    assert(stack->ptr->type == AST_LITERAL_INT);
    int32_t i1 = stack->ptr->intVal;
    stack->ptr--;

    return i1;
}


void ast_execute(ast_node_t *n, ast_parameter_t *param0, ast_stack_t *stack) {
    int32_t i1, i2, res;
    ast_literal_t lit,lit2;
    ccpeed_err_t cerr;

    assert(n);

    switch (n->op) {
        case AST_TYPE_CONDITIONAL:
            // Operand1 is the expression
            assert(n->num_children == 3 || n->num_children == 2);
            ast_execute(n->children, param0, stack);
            if (ast_stack_popint(stack)) {
                // Its truthy
                ast_execute(n->children + 1, param0, stack);
            } else if (n->num_children == 3) {
                // Its falsey
                ast_execute(n->children + 2, param0, stack);
            } else {
                ast_stack_push((ast_literal_t *) &null_ast, stack);
            }
            break;

        case AST_TYPE_NOT:
            assert(n->num_children == 1);
            ast_execute(n->children, param0, stack);
            ast_stack_push_intlike(AST_LITERAL_INT, !ast_stack_popint(stack), stack);
            break;

        case AST_TYPE_LITERAL:
            ast_stack_push(&(n->value), stack);
            break;

        case AST_TYPE_PARAM:
            // Right now we only support one argument.
            assert(n->num_children == 1);
            ast_execute(n->children, param0, stack);
            i1 = ast_stack_popint(stack);
            assert(i1 == 0);
            lit.type = AST_LITERAL_OBJECT;
            lit.ptr = param0;
            ast_stack_push(&lit, stack);
            break;

        case AST_TYPE_INDEX:
            assert(n->num_children == 2);
            ast_execute(n->children, param0, stack);
            ast_execute(n->children+1, param0, stack);
            i1 = ast_stack_popint(stack);
            ast_stack_pop(&lit, stack);
            assert(lit.type == AST_LITERAL_OBJECT);

            cerr = param0->field_fetcher(((ast_parameter_t *) lit.ptr)->ptr, i1, &lit2);
            assert(cerr == CCPEED_NO_ERR);

            ast_stack_push(&lit2, stack);
            break;

        case AST_TYPE_ARRAY:
            // Arrays are put onto the stack in reverse order, so that they can be easily popped back off
            // in forward order. 
            for (int i = n->num_children-1; i >= 0; i--) {
                ast_execute(n->children+i, param0, stack);
            }
            lit.type = AST_LITERAL_ARRAY;
            lit.intVal = n->num_children;
            ast_stack_push(&lit, stack);
            break;

        case AST_TYPE_MAP:
            // Maps must have an even number of children.
            assert(n->num_children % 2 == 0);
            for (int i = n->num_children-1; i >= 0; i--) {
                ast_execute(n->children+i, param0, stack);
            }
            lit.type = AST_LITERAL_MAP;
            lit.intVal = n->num_children/2;
            ast_stack_push(&lit, stack);
            break;

        default:
            // Everything else is a binary arithmetic operator (or at least it should be).
            assert(n->num_children == 2);
            ast_execute(n->children, param0, stack);
            ast_execute(n->children+1, param0, stack);
            i2 = ast_stack_popint(stack);
            i1 = ast_stack_popint(stack);
            switch (n->op) {
                case AST_TYPE_EQUALS:
                    res = i1 == i2;
                    break;
                case AST_TYPE_PLUS:
                    res = i1 + i2;
                    break;
                case AST_TYPE_MINUS:
                    res = i1 - i2;
                    break;
                case AST_TYPE_DIVIDE:
                    res = i1 / i2;
                    break;
                case AST_TYPE_MULTIPLY:
                    res = i1 * i2;
                    break;
                case AST_TYPE_MODULUS:
                    res = i1 % i2;
                    break;
                default:
                    assert(false);
                    break;
            }
            ast_stack_push_intlike(AST_LITERAL_INT, res, stack);
            break;
    }
}


CborError ast_serialise_to_cbor(ast_node_t *node, CborEncoder *enc) {
    CborEncoder nodeEncoder;
    CborError err;

    switch (node->op) {
        case AST_TYPE_LITERAL:
            switch (node->value.type) {
                case AST_LITERAL_INT:
                    err = cbor_encode_int(enc, node->value.intVal);
                    if (err != CborNoError) {
                        return err;
                    }
                    break;
                case AST_LITERAL_NULL:
                    err = cbor_encode_null(enc);
                    if (err != CborNoError) {
                        return err;
                    }
                    break;
                case AST_LITERAL_BYTESTRING:
                    err = cbor_encode_byte_string(enc, node->value.bytes.data, node->value.bytes.sz);
                    if (err != CborNoError) {
                        return err;
                    }
                    break;
                default:
                    return CborErrorImproperValue;
            }
            break;
        default:
            err = cbor_encoder_create_array(enc, &nodeEncoder, node->num_children+1);
            if (err != CborNoError) {
                return err;
            }
            err = cbor_encode_uint(&nodeEncoder, node->op);
            if (err != CborNoError) {
                return err;
            }
            for (int i = 0; i < node->num_children; i++) {
                ast_serialise_to_cbor(node->children+i, &nodeEncoder);
            }
            err = cbor_encoder_close_container(enc, &nodeEncoder);
            if (err != CborNoError) {
                return err;
            }
            break;
    }
    return CborNoError;
}

CborError ast_parse_from_cbor(CborValue *val, ast_node_t *node) { 
    uint64_t ui;
    CborError err;
    CborValue nodeIter;
    size_t arrayLength;
    bool bVal;
    int iVal;

    if (cbor_value_is_array(val)) {
        cbor_value_get_array_length(val, &arrayLength);
        if (arrayLength < 1) {
            ESP_LOGW(TAG, "Operator array with no operator");
            return CborErrorImproperValue;
        }

        err = cbor_value_enter_container(val, &nodeIter);
        if (err != CborNoError) {
            ESP_LOGW(TAG, "Error entering container: %d", err);
            return err;
        }

        err = cbor_value_get_uint64(&nodeIter, &ui);
        if (err != CborNoError) {
            ESP_LOGW(TAG, "Error getting operand: %d", err);
            return err;
        }
        node->op = ui;
        ESP_LOGD(TAG, "Operator %d ", node->op);
        err = cbor_value_advance(&nodeIter);
        if (err != CborNoError) {
            ESP_LOGD(TAG, "Error advancing after operator: %d", err);
            return err;
        }
        arrayLength--;

        // Most nodes have a specified number of children.  ARRAY and MAP do not.
        if (node->op != AST_TYPE_ARRAY && node->op != AST_TYPE_MAP) {
            if (expected_children[node->op] != arrayLength) {
                ESP_LOGW(TAG, "Improper number of children for operator %d - Expected %d but got %d", node->op, expected_children[node->op], arrayLength);
                return CborErrorImproperValue;
            }
        }

        node->num_children = arrayLength;
        node->children = malloc(sizeof(ast_node_t) * node->num_children);
        if (!node->children) {
            ESP_LOGW(TAG, "Could not create child array");
            return CborErrorOutOfMemory;
        }

        for (int i = 0; i < node->num_children; i++) {
            err = ast_parse_from_cbor(&nodeIter, node->children+i);
            if (err != CborNoError) {
                ESP_LOGW(TAG, "Error parsing child node %d: %d", i, err);
                free(node->children);
                return err;
            }
        }
        err = cbor_value_leave_container(val, &nodeIter);
        if (err != CborNoError) {
            free(node->children);
            ESP_LOGW(TAG, "Error leaving container: %d", err);
            return err;
        }
    } else {
        node->children = NULL;
        node->num_children = 0;
        node->op = AST_TYPE_LITERAL;
        ui = cbor_value_get_type(val);

        switch (ui) {
            case CborIntegerType:
                ESP_LOGD(TAG, "Literal Integer");
                node->value.type = AST_LITERAL_INT;
                err = cbor_value_get_int(val, &iVal);
                if (err != CborNoError) {
                    ESP_LOGW(TAG, "Error parsing integer value");
                    return err;
                }
                ESP_LOGD(TAG, "Value is %d", iVal);
                node->value.intVal = iVal;
                break;

            case CborBooleanType:
                ESP_LOGD(TAG, "Literal Boolean");
                node->value.type = AST_LITERAL_INT;
                err = cbor_value_get_boolean(val, &bVal);
                if (err != CborNoError) {
                    return err;
                }
                node->value.intVal = bVal;
                break;

            // We reate Null and Undefined the same. 
            case CborNullType:
                ESP_LOGD(TAG, "Literal Null");
                node->value.type = AST_LITERAL_NULL;
                break;

            case CborByteStringType:
                ESP_LOGD(TAG, "Literal Byte string");
                node->value.type = AST_LITERAL_NULL;
                err = cbor_value_calculate_string_length(val, &node->value.bytes.sz);
                if (err != CborNoError) {
                    return err;
                }

                node->value.bytes.data = malloc(node->value.bytes.sz);
                if (!node->value.bytes.data) {
                    return CborErrorOutOfMemory;
                }

                err = cbor_value_copy_byte_string(val, node->value.bytes.data, &node->value.bytes.sz, NULL); 
                if (err != CborNoError) {
                    free(node->value.bytes.data);
                    return err;
                }
                break;

            // case CborUndefinedType:
            // case CborTextStringType:
            default:
                ESP_LOGW(TAG, "Unknown tag type %llu", ui);
                // All other types are invalid.
                return CborErrorImproperValue;
        }

        // Now advance the pointer past the value we just read.
        err = cbor_value_advance(val);
        if (err != CborNoError) {
            ESP_LOGW(TAG, "Error advancing: %d", err);
            free(node->children);
            return err;
        }

    }

    return CborNoError;
}


void ast_free(ast_node_t *node) {
    if (node->op == AST_TYPE_LITERAL && node->value.type == AST_LITERAL_BYTESTRING) {
        free(node->value.bytes.data);
    }
    for (int i = 0; i < node->num_children; i++) {
        ast_free(node->children+i);
    }
    if (node->children) {
        free(node->children);
    }
}



void ast_set_null(ast_node_t *node) {
    node->op = AST_TYPE_LITERAL;
    node->num_children = 0;
    node->value.type = AST_LITERAL_NULL;
    node->value.ptr = NULL;
}