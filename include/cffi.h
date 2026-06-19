#ifndef BEER_CFFI_H
#define BEER_CFFI_H

#ifdef BEER_CFFI

/* Initialize the CPointer type (register destructor).
 * Called from namespace_init() before core_register_ffi(). */
void cpointer_init(void);

/* Register beer.ffi native functions into the runtime.
 * Called from namespace_init() when compiled with CFFI=1. */
void core_register_ffi(void);

#endif /* BEER_CFFI */

#endif /* BEER_CFFI_H */
