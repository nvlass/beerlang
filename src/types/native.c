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

    nf->arity = arity;
    nf->fn_ptr = fn_ptr;
    nf->name = name ? strdup(name) : NULL;

    /* Natives are registered once and live for the whole run. Immortal, so
     * calling + doesn't write its refcount on every call. */
    Value v = tag_pointer(nf);
    object_make_immortal(v);
    return v;
}

/* Get native function name */
const char* native_function_name(Value fn) {
    assert(is_native_function(fn));
    NativeFunction* nf = (NativeFunction*)untag_pointer(fn);
    return nf->name ? nf->name : "<native>";
}

