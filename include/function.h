/* Function - Bytecode functions and closures
 *
 * Functions are heap-allocated objects containing bytecode and
 * optional closure environment for captured variables.
 */

#ifndef BEERLANG_FUNCTION_H
#define BEERLANG_FUNCTION_H

#include "value.h"

/* Function object structure */
typedef struct Function {
    struct Object header;    /* header.size = arity (-1 for variadic) */
    uint32_t code_offset;    /* Offset in bytecode where function starts */
    uint16_t n_locals;       /* Number of local variable slots */
    uint16_t n_closed;       /* Number of closed-over values */
    /* Self-contained execution context (set after compilation) */
    uint8_t* code;           /* Pointer to bytecode array */
    int code_size;           /* Size of bytecode array */
    Value* constants;        /* Pointer to constants array */
    int num_constants;       /* Number of constants */
    char* name;              /* Function name (owned, always non-NULL) */
    char* ns_name;           /* Defining namespace name (owned, NULL = current) */
    Value    meta;           /* Metadata map (or nil) */
    Value    closed[];       /* Flexible array: closure environment */
} Function;

/* Create a new function (no closure) */
Value function_new(int arity, uint32_t code_offset, uint16_t n_locals,
                   const char* name);

/* Create a closure (function with captured variables) */
Value function_new_closure(int arity, uint32_t code_offset, uint16_t n_locals,
                          uint16_t n_closed, Value* closed_values,
                          const char* name);

/* Set the execution context (bytecode + constants) on a function.
 * Called after compilation to make the function self-contained.
 * The function does NOT own these pointers - caller must keep them alive. */
void function_set_code(Value fn, uint8_t* code, int code_size,
                       Value* constants, int num_constants);

/* Get function properties */
int function_arity(Value fn);
uint32_t function_code_offset(Value fn);
uint16_t function_n_locals(Value fn);
uint16_t function_n_closed(Value fn);
Value function_get_closed(Value fn, uint16_t idx);

/* Get execution context */
uint8_t* function_get_code(Value fn);
int function_get_code_size(Value fn);
Value* function_get_constants(Value fn);
int function_get_num_constants(Value fn);

/* Get function name (always non-NULL) */
const char* function_name(Value fn);

/* Get/set defining namespace */
const char* function_ns_name(Value fn);
void function_set_ns_name(Value fn, const char* ns_name);

/* Get/set metadata */
Value function_get_meta(Value fn);
void function_set_meta(Value fn, Value meta);

/* Type check */
bool is_function(Value v);

#endif /* BEERLANG_FUNCTION_H */
