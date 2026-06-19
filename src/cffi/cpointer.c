#ifdef BEER_CFFI

#include "cpointer.h"
#include "memory.h"

static void cpointer_destructor(struct Object* obj) {
    (void)obj;
    /* CPointer does not own its memory — the caller manages lifetime.
     * Use ffi/free or ffi/close before releasing the cpointer. */
}

void cpointer_init(void) {
    object_register_destructor(TYPE_CPOINTER, cpointer_destructor);
}

Value cpointer_new(void* ptr) {
    CPointer* cp = (CPointer*)object_alloc(TYPE_CPOINTER, sizeof(CPointer));
    cp->ptr = ptr;
    return tag_pointer(cp);
}

void* cpointer_get(Value v) {
    return ((CPointer*)untag_pointer(v))->ptr;
}

#endif /* BEER_CFFI */
