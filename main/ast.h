#pragma once


#include <cbor.h>
#include "ccpeed_err.h"



typedef enum {
    AST_TYPE_CONDITIONAL, // ? operator
    AST_TYPE_EQUALS,      // == operator
    AST_TYPE_MODULUS,     // % operator
    AST_TYPE_PLUS,        // + operator
    AST_TYPE_MINUS,       // - operator
    AST_TYPE_MULTIPLY,    // * operator
    AST_TYPE_DIVIDE,      // / operator
    AST_TYPE_NOT,         // ! operator
    AST_TYPE_PARAM,       // Push the numbered parameter onto the stack. Only support index 0 right now
    AST_TYPE_INDEX,       // [] or . - fetch a subitem - First is a map or array, second is an index.
    AST_TYPE_LITERAL,
    AST_TYPE_ARRAY,
    AST_TYPE_MAP,
} ast_operator_type_t;




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

typedef struct {
    void *ptr;
    ccpeed_err_t (*field_fetcher)(void *ptr, unsigned int param_no, ast_literal_t *out);
} ast_parameter_t;

typedef struct ast_node_t {
    ast_operator_type_t op;
    ast_literal_t value;
    struct ast_node_t *children;
    size_t num_children;
} ast_node_t;

#define AST_NODE_INIT { \
    .op = AST_TYPE_LITERAL, \
    .value.type = AST_LITERAL_NULL, \
    .children = NULL, \
    .num_children = 0 \
}


typedef struct ast_stack_t {
    ast_literal_t *base;
    ast_literal_t *ptr;
    size_t capacity;
} ast_stack_t;

bool ast_stack_alloc(ast_stack_t *stack, size_t sz);
void ast_stack_free(ast_stack_t *stack);

CborError ast_parse_from_cbor(CborValue *val, ast_node_t *node);
void ast_free(ast_node_t *node);
CborError ast_serialise_to_cbor(ast_node_t *node, CborEncoder *enc);
void ast_execute(ast_node_t *n, ast_parameter_t *param0, ast_stack_t *stack);
void ast_set_null(ast_node_t *node);
