#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include "ast.h"


/*
 Exmaple AST:
 evt.clickCount == 1 ? [[], ASPECT_ON_OFF, TOGGLE] : [[], ASPECT_BRIGHTNESS, RECALL_MAX]

CONDITIONAL:
    EQUALS:
        INDEX:
            PARAM(0)
            LITERAL(CLICK_COUNT_IDX)
        LITERAL(1)

    LITERAL(ARRAY):
        LITERAL(ARRAY):
            UINT(GTIN)
            BYTESTRING(abcdef),
            UINT(SERIAL)
            BYTESTRING(abcdef),
        UINT(ASPECT_ON_OFF),
        UINT(TOGGLE)

    ARRAY:
        ARRAY:
            INT(GTIN)
            BYTESTRING(abcdef),
            INT(SERIAL)
            BYTESTRING(abcdef),
        UINT(ASPECT_BRIGHTNESS)
        UINT(RECALL_MAX)


= [CONDITIONAL, [EQUALS, [INDEX, [PARAM 0], 0], 1], [ARRAY, [ARRAY, GTIN, BYTESTRING, SERIAL, BYTESTRING], ASPECT_ON_OFF, TOGGLE], [ARRAY, [ARRAY, GTIN, BYTESTRING, SERIAL, BYTESTRING], ASPECT_BRIGHTNESS, RECALL_MAX]]

Another example

[[], ASPECT_BRIGHTNESS, evt.clickCount % 2 ? 1 : -1]

ARRAY:
    ARRAY:
        UINT(GTIN)
        BYTESTRING(abcdef),
        UINT(SERIAL)
        BYTESTRING(abcdef),
    UINT(ASPECT_BRIGHTNESS)
    CONDITIONAL:
        MODULO:
            INDEX:
                PARAM(0)
                UINT(CLICK_COUNT_IDX)
            2
        UINT(1)
        INT(-1)



When dealing with an array or map, we want to have multiple values on the stack.  You can do it by pushing
each of its individual values, then pushing a final Array/Map marker node, with a size. 
 */





typedef enum {
    AST_TYPE_CONDITIONAL, // ? operator
    AST_TYPE_EQUALS,      // == operator
    AST_TYPE_MODULUS,     // % operator
    AST_TYPE_PLUS,
    AST_TYPE_MINUS,
    AST_TYPE_MULTIPLY,
    AST_TYPE_DIVIDE,
    AST_TYPE_NOT,
    AST_TYPE_PARAM,       // Push the numbered parameter onto the stack.
    AST_TYPE_INDEX,       // [] or . - fetch a subitem - First is a map or array, second is an index.
    AST_TYPE_LITERAL,
    AST_TYPE_ARRAY,
    AST_TYPE_MAP,
} ast_operator_type_t;

static const size_t expected_children[] = {
    3,
    2,
    2,
    2,
    2,
    2,
    2,
    1,
    2,
    2
};


typedef enum {
    AST_LITERAL_NULL,
    AST_LITERAL_INT,
    AST_LITERAL_BYTESTRING,
    AST_LITERAL_ARRAY,
    AST_LITERAL_MAP,
    AST_LITERAL_OBJECT,
} ast_literal_type_t;

typedef struct {
    uint8_t *data;
    size_t sz; 
} bytestring_t;

typedef struct {
    ast_literal_type_t type;
    // Can be values or children nodes, or whatever, depending on type
    union {
        int32_t intVal;
        bytestring_t bytes; // Used for both byte strings and character strings.
        void *ptr;   // Used for object. 
    };
} ast_literal_t;



typedef struct ast_node_t {
    ast_operator_type_t op;
    ast_literal_t value;
    struct ast_node_t *children;
    size_t num_children;
} ast_node_t;


typedef struct {
    ast_literal_t *base;
    ast_literal_t *ptr;
    size_t capacity;
} ast_stack_t;


static const ast_literal_t nullVal = {
    .type = AST_LITERAL_NULL,
    .intVal = 0,
};



void ast_stack_push_intlike(ast_literal_type_t type, int32_t val, ast_stack_t *stack) {
    assert(stack->ptr - stack->base < stack->capacity);
    stack->ptr->type = type;
    stack->ptr->intVal = val;
    stack->ptr++;
}

void ast_stack_push(ast_literal_t *literal, ast_stack_t *stack) {
    assert(stack->ptr - stack->base < stack->capacity);
    memcpy(stack->ptr, literal, sizeof(ast_literal_t));
    stack->ptr++;
}

void ast_stack_pop(ast_literal_t *val, ast_stack_t *stack) {
    assert (stack->ptr > stack->base);
    memcpy(val, stack->ptr, sizeof(ast_literal_t));
    stack->ptr--;
}

int32_t ast_stack_popint(ast_stack_t *stack) {
    assert(stack->ptr > stack->base);
    assert(stack->ptr->type == AST_LITERAL_INT);
    int32_t i1 = stack->ptr->intVal;
    stack->ptr--;

    return i1;
}


void ast_execute(ast_node_t *n, void *param0, ast_stack_t *stack) {
    int32_t i1, i2, res;
    ast_literal_t lit;

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
                ast_stack_push((ast_literal_t *) &nullVal, stack);
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

            switch (i1) {
                case 0:
                    ast_stack_push_intlike(AST_LITERAL_INT, ((button_event_t *) lit.ptr)->clickCount, stack);
                    break;
                default:
                    assert(false);
            }
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


    
    return CborNoError;
}

CborError ast_parse_from_cbor(CborValue *val, ast_node_t *node) { 
    uint64_t ui;
    CborError err;
    CborValue nodeIter;
    size_t arrayLength;
    bool bVal;
    int iVal;
    size_t szVal;

    if (cbor_value_is_array(val)) {
        cbor_value_get_array_length(val, &arrayLength);
        err = cbor_value_enter_container(val, &nodeIter);
        if (err != CborNoError) {
            return err;
        }

        err = cbor_value_get_uint64(&nodeIter, &ui);
        if (err != CborNoError) {
            return err;
        }
        node->op = ui;
        err = cbor_value_advance(&nodeIter);
        if (err != CborNoError) {
            return err;
        }
        arrayLength--;

        // Most nodes have a specified number of children.  ARRAY and MAP do not.
        if (node->op != AST_TYPE_ARRAY && node->op != AST_TYPE_MAP) {
            if (expected_children[node->op] != arrayLength) {
                return CborErrorImproperValue;
            }
        }

        node->num_children = arrayLength;
        node->children = malloc(sizeof(ast_node_t) * node->num_children);
        if (!node->children) {
            return CborErrorOutOfMemory;
        }

        for (int i = 0; i < node->num_children; i++) {
            ast_parse_from_cbor(&nodeIter, node->children+i);
            err = cbor_value_advance(&nodeIter);
            if (err != CborNoError) {
                free(node->children);
                return err;
            }
        }

        err = cbor_value_leave_container(val, &nodeIter);
        if (err != CborNoError) {
            free(node->children);
            return err;
        }
    } else {
        node->children = NULL;
        node->num_children = 0;
        node->op = AST_TYPE_LITERAL;

        switch (cbor_value_get_type(val)) {
            case CborIntegerType:
                node->value.type = AST_LITERAL_INT;
                err = cbor_value_get_int(&nodeIter, &iVal);
                if (err != CborNoError) {
                    return err;
                }
                node->value.intVal = iVal;
                break;

            case CborBooleanType:
                node->value.type = AST_LITERAL_INT;
                err = cbor_value_get_boolean(&nodeIter, &bVal);
                if (err != CborNoError) {
                    return err;
                }
                node->value.intVal = bVal;
                break;

            // We reate Null and Undefined the same. 
            case CborNullType:
                node->value.type = AST_LITERAL_NULL;
                break;

            case CborByteStringType:
                node->value.type = AST_LITERAL_NULL;
                err = cbor_value_calculate_string_length(&nodeIter, &node->value.bytes.sz);
                if (err != CborNoError) {
                    return err;
                }

                node->value.bytes.data = malloc(node->value.bytes.sz);
                if (!node->value.bytes.data) {
                    return CborErrorOutOfMemory;
                }

                err = cbor_value_copy_byte_string(&nodeIter, node->value.bytes.data, &node->value.bytes.sz, NULL); 
                if (err != CborNoError) {
                    free(node->value.bytes.data);
                    return err;
                }
                break;

            // case CborUndefinedType:
            // case CborTextStringType:
            default:
                // All other types are invalid.
                return CborErrorImproperValue;
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

