/* Native Functions - C functions callable from beerlang
 *
 * Native functions are heap-allocated objects that wrap C function pointers.
 * They allow extending beerlang with functionality implemented in C.
 */

#ifndef BEERLANG_NATIVE_H
#define BEERLANG_NATIVE_H

#include "value.h"

/* Forward declaration */
typedef struct VM VM;

/* Native function signature
 *
 * Parameters:
 *   vm   - VM instance (for error reporting, stack access, etc.)
 *   argc - Number of arguments
 *   argv - Array of argument values
 *
 * Returns:
 *   The result value (or VALUE_NIL on error)
 *   On error, should call vm_error() and return VALUE_NIL
 */
typedef Value (*NativeFn)(VM* vm, int argc, Value* argv);

/* Native function object structure */
typedef struct NativeFunction {
    struct Object header;    /* header.size = arity (-1 for variadic) */
    NativeFn fn_ptr;        /* C function pointer */
    const char* name;       /* Function name (for debugging/errors) */
} NativeFunction;

/* Create a new native function
 *
 * Parameters:
 *   arity  - Number of arguments (-1 for variadic)
 *   fn_ptr - C function pointer
 *   name   - Function name (will be copied)
 */
Value native_function_new(int arity, NativeFn fn_ptr, const char* name);

/* Get native function properties */
int native_function_arity(Value fn);
NativeFn native_function_ptr(Value fn);
const char* native_function_name(Value fn);

/* Type check */
bool is_native_function(Value v);

/* Initialize native function type (register destructor) */
void native_function_init(void);

#endif /* BEERLANG_NATIVE_H */
