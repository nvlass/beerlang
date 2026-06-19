#ifndef BEER_CPOINTER_H
#define BEER_CPOINTER_H

#ifdef BEER_CFFI

#include "value.h"

/* Wraps a raw void* for use as a beerlang value.
 * The CPointer does NOT own the pointed-to memory — callers are responsible
 * for lifetime (ffi/free, ffi/close, or the library's own cleanup API). */
typedef struct CPointer {
    struct Object header;
    void* ptr;
} CPointer;

void   cpointer_init(void);
Value  cpointer_new(void* ptr);
void*  cpointer_get(Value v);

static inline bool is_cpointer(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_CPOINTER;
}

#endif /* BEER_CFFI */
#endif /* BEER_CPOINTER_H */
