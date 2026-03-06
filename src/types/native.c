/* Native Function Implementation */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "native.h"
#include "memory.h"
#include "value.h"

/* Destructor for native functions */
static void native_function_destructor(struct Object* obj) {
    NativeFunction* nf = (NativeFunction*)obj;

    /* Free the name string (was strdup'd) */
    if (nf->name) {
        free((void*)nf->name);
    }
}

/* Initialize native function type (idempotent - safe to call multiple times) */
void native_function_init(void) {
    object_register_destructor(TYPE_NATIVE_FN, native_function_destructor);
}

/* Create a new native function */
Value native_function_new(int arity, NativeFn fn_ptr, const char* name) {
    native_function_init();

    NativeFunction* nf = (NativeFunction*)object_alloc(TYPE_NATIVE_FN, sizeof(NativeFunction));
    if (!nf) {
        return VALUE_NIL;
    }

    nf->header.size = arity;  /* Store arity in header.size */
    nf->fn_ptr = fn_ptr;
    nf->name = name ? strdup(name) : NULL;

    return tag_pointer(nf);
}

/* Get native function arity */
int native_function_arity(Value fn) {
    assert(is_native_function(fn));
    NativeFunction* nf = (NativeFunction*)untag_pointer(fn);
    return (int)nf->header.size;
}

/* Get native function pointer */
NativeFn native_function_ptr(Value fn) {
    assert(is_native_function(fn));
    NativeFunction* nf = (NativeFunction*)untag_pointer(fn);
    return nf->fn_ptr;
}

/* Get native function name */
const char* native_function_name(Value fn) {
    assert(is_native_function(fn));
    NativeFunction* nf = (NativeFunction*)untag_pointer(fn);
    return nf->name ? nf->name : "<native>";
}

/* Type check */
bool is_native_function(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_NATIVE_FN;
}
