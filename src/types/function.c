/* Function implementation */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "function.h"
#include "memory.h"
#include "beerlang.h"

/* Destructor for function objects */
static void function_destructor(struct Object* obj) {
    Function* fn = (Function*)obj;

    /* Free owned name string */
    free(fn->name);
    free(fn->ns_name);

    /* Release metadata */
    if (is_pointer(fn->meta)) {
        object_release(fn->meta);
    }

    /* Release all closed-over values */
    for (uint16_t i = 0; i < fn->n_closed; i++) {
        if (is_pointer(fn->closed[i])) {
            object_release(fn->closed[i]);
        }
    }
}

/* Create a new function (no closure) */
Value function_new(int arity, uint32_t code_offset, uint16_t n_locals,
                   const char* name) {
    return function_new_closure(arity, code_offset, n_locals, 0, NULL, name);
}

/* Create a closure (function with captured variables) */
Value function_new_closure(int arity, uint32_t code_offset, uint16_t n_locals,
                          uint16_t n_closed, Value* closed_values,
                          const char* name) {
    /* Allocate function with space for closure */
    size_t size = sizeof(Function) + (n_closed * sizeof(Value));
    Function* fn = (Function*)object_alloc(TYPE_FUNCTION, size);
    if (!fn) {
        return VALUE_NIL;
    }

    /* Set header size to arity */
    fn->header.size = (uint32_t)arity;
    fn->code_offset = code_offset;
    fn->n_locals = n_locals;
    fn->n_closed = n_closed;
    fn->code = NULL;
    fn->code_size = 0;
    fn->constants = NULL;
    fn->num_constants = 0;
    fn->name = strdup(name ? name : "anonymous-fn");
    fn->ns_name = NULL;
    fn->meta = VALUE_NIL;

    /* Copy and retain closed-over values */
    if (n_closed > 0 && closed_values) {
        for (uint16_t i = 0; i < n_closed; i++) {
            fn->closed[i] = closed_values[i];
            if (is_pointer(closed_values[i])) {
                object_retain(closed_values[i]);
            }
        }
    }

    /* Register destructor */
    object_register_destructor(TYPE_FUNCTION, function_destructor);

    return tag_pointer(fn);
}

/* Get function arity */
int function_arity(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return (int)f->header.size;
}

/* Get function code offset */
uint32_t function_code_offset(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->code_offset;
}

/* Get number of locals */
uint16_t function_n_locals(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->n_locals;
}

/* Get number of closed variables */
uint16_t function_n_closed(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->n_closed;
}

/* Get closed-over value */
Value function_get_closed(Value fn, uint16_t idx) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    assert(idx < f->n_closed);
    return f->closed[idx];
}

/* Set execution context on function */
void function_set_code(Value fn, uint8_t* code, int code_size,
                       Value* constants, int num_constants) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    f->code = code;
    f->code_size = code_size;
    f->constants = constants;
    f->num_constants = num_constants;
}

/* Get execution context */
uint8_t* function_get_code(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->code;
}

int function_get_code_size(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->code_size;
}

Value* function_get_constants(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->constants;
}

int function_get_num_constants(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->num_constants;
}

/* Get function name (always non-NULL) */
const char* function_name(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->name;
}

/* Get defining namespace */
const char* function_ns_name(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->ns_name;
}

/* Set defining namespace */
void function_set_ns_name(Value fn, const char* ns_name) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    free(f->ns_name);
    f->ns_name = ns_name ? strdup(ns_name) : NULL;
}

/* Get metadata */
Value function_get_meta(Value fn) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    return f->meta;
}

/* Set metadata */
void function_set_meta(Value fn, Value meta) {
    assert(is_function(fn));
    Function* f = (Function*)untag_pointer(fn);
    Value old = f->meta;
    f->meta = meta;
    if (is_pointer(meta)) object_retain(meta);
    if (is_pointer(old)) object_release(old);
}

/* Type check */
bool is_function(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_FUNCTION;
}
