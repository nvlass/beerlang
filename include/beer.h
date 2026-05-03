/*
 * beer.h — Embeddable C API for Beerlang
 *
 * Link against libbeerlang.a (or libbeerlang.so) and include this header.
 *
 * Quick start:
 *
 *   #include "beer.h"
 *
 *   int main(void) {
 *       BeerState* B = beer_open();
 *
 *       beer_do_string(B, "(println \"hello from beerlang!\")");
 *
 *       beer_close(B);
 *       return 0;
 *   }
 *
 * Compile:
 *   gcc myapp.c -Iinclude -Lbuild -lbeerlang -lm -lpthread -o myapp
 *
 * Licensing note:
 *   libbeerlang embeds mini-gmp (LGPL v3) and ulog (MIT).
 *   Static linking with mini-gmp requires users be able to relink against
 *   a replacement GMP implementation (LGPL requirement).
 */

#ifndef BEER_H
#define BEER_H

#include <stdint.h>
#include "value.h"   /* Value / BeerValue type */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Opaque handles                                                       */
/* ------------------------------------------------------------------ */

typedef struct BeerState BeerState;  /* lifecycle handle      */
typedef struct VM        BeerVM;     /* VM handle (in natives) */

/* BeerValue is the same struct as the internal Value — no conversion. */
typedef Value BeerValue;

/* Native function signature — identical to internal NativeFn.
 * Use beer_vm_error(vm, msg) to report errors; return beer_nil() on error. */
typedef BeerValue (*BeerNativeFn)(BeerVM* vm, int argc, BeerValue* argv);

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

/* Open a Beerlang state: initialise allocator, intern tables,
 * namespaces, all native functions, scheduler, reactor, and core.beer.
 * Idempotent for the underlying global state; safe to call once. */
BeerState* beer_open(void);

/* Shut down: free scheduler, namespace registry, etc. */
void beer_close(BeerState* B);

/* ------------------------------------------------------------------ */
/* Load path                                                            */
/* ------------------------------------------------------------------ */

/* Prepend a colon-separated path string to *load-path*.
 * Equivalent to setting BEERPATH before startup. */
void beer_add_load_path(BeerState* B, const char* path);

/* ------------------------------------------------------------------ */
/* Eval                                                                 */
/* ------------------------------------------------------------------ */

/* Compile and run all top-level forms in src. Returns 0 on success,
 * 1 on error (retrieve message with beer_error). */
int beer_do_string(BeerState* B, const char* src);

/* Compile and run all forms in a file. Returns 0 on success, 1 on error. */
int beer_do_file(BeerState* B, const char* path);

/* Eval a single expression string; return its value.
 * Returns beer_nil() on error; check beer_error() to distinguish nil result. */
BeerValue beer_eval_expr(BeerState* B, const char* expr);

/* ------------------------------------------------------------------ */
/* Lookup and call                                                      */
/* ------------------------------------------------------------------ */

/* Look up a var. qualified may be "ns/name" or bare "name" (searched in
 * beer.core then user). Returns beer_nil() if not found. */
BeerValue beer_lookup(BeerState* B, const char* qualified);

/* Call a beerlang function from C. Returns result or beer_nil() on error. */
BeerValue beer_call(BeerState* B, BeerValue fn, int argc, BeerValue* argv);

/* ------------------------------------------------------------------ */
/* Native registration                                                  */
/* ------------------------------------------------------------------ */

/* Register a C function as a beerlang native in namespace ns.
 * Creates the namespace if it doesn't exist.
 * The fn pointer is cast directly — no trampoline overhead. */
void beer_register(BeerState* B, const char* ns, const char* name,
                   BeerNativeFn fn);

/* ------------------------------------------------------------------ */
/* Scheduler                                                            */
/* ------------------------------------------------------------------ */

/* Run all pending tasks (spawned via spawn) to completion.
 * Call after beer_do_string/file if you spawned tasks and need their
 * results before continuing. beer_do_* already drains automatically. */
void beer_run(BeerState* B);

/* ------------------------------------------------------------------ */
/* Error handling                                                       */
/* ------------------------------------------------------------------ */

/* Return last error message, or NULL if no error.
 * Valid until the next beer_* call or beer_clear_error. */
const char* beer_error(BeerState* B);

/* Clear the stored error. */
void beer_clear_error(BeerState* B);

/* Report an error from inside a BeerNativeFn. */
void beer_vm_error(BeerVM* vm, const char* msg);

/* ------------------------------------------------------------------ */
/* Value constructors  (C → Beerlang)                                  */
/* ------------------------------------------------------------------ */

static inline BeerValue beer_int(int64_t n)    { return make_fixnum(n); }
static inline BeerValue beer_float(double d)   { return make_float(d);  }
static inline BeerValue beer_bool(int b)        { return b ? VALUE_TRUE : VALUE_FALSE; }
static inline BeerValue beer_nil(void)          { return VALUE_NIL; }

/* Allocate a beerlang string from a C string. */
BeerValue beer_string(const char* s);

/* Return an interned keyword (":name" — do not include the colon). */
BeerValue beer_keyword(const char* name);

/* Return an interned symbol. */
BeerValue beer_symbol(const char* name);

/* ------------------------------------------------------------------ */
/* Value inspectors  (Beerlang → C)                                    */
/* ------------------------------------------------------------------ */

static inline int beer_is_nil    (BeerValue v) { return v.tag == TAG_NIL;   }
static inline int beer_is_int    (BeerValue v) { return is_fixnum(v);        }
static inline int beer_is_float  (BeerValue v) { return is_float(v);         }
static inline int beer_is_bool   (BeerValue v) { return v.tag == TAG_TRUE || v.tag == TAG_FALSE; }
static inline int beer_is_string (BeerValue v) { return is_pointer(v) && object_type(v) == TYPE_STRING; }
static inline int beer_is_keyword(BeerValue v) { return is_pointer(v) && object_type(v) == TYPE_KEYWORD; }
static inline int beer_is_symbol (BeerValue v) { return is_pointer(v) && object_type(v) == TYPE_SYMBOL; }
static inline int beer_is_map    (BeerValue v) { return is_pointer(v) && object_type(v) == TYPE_HASHMAP; }
static inline int beer_is_vec    (BeerValue v) { return is_pointer(v) && object_type(v) == TYPE_VECTOR; }
static inline int beer_is_list   (BeerValue v) { return is_pointer(v) && object_type(v) == TYPE_CONS; }

static inline int64_t beer_to_int(BeerValue v) {
    if (is_fixnum(v)) return untag_fixnum(v);
    if (is_float(v))  return (int64_t)untag_float(v);
    return 0;
}
static inline double beer_to_double(BeerValue v) {
    if (is_float(v))  return untag_float(v);
    if (is_fixnum(v)) return (double)untag_fixnum(v);
    return 0.0;
}
static inline int beer_to_bool(BeerValue v) { return v.tag != TAG_FALSE && v.tag != TAG_NIL; }

/* Return a C string pointer. Valid as long as v is reachable/retained. */
const char* beer_to_cstring(BeerValue v);

/* ------------------------------------------------------------------ */
/* Collection access                                                    */
/* ------------------------------------------------------------------ */

/* Number of elements (list, vector, string, map). -1 if not applicable. */
int beer_length(BeerValue v);

/* Element at index i in a vector or list (0-based). beer_nil() if OOB. */
BeerValue beer_nth(BeerValue v, int i);

/* Look up key in a map. beer_nil() if not found or v is not a map. */
BeerValue beer_get(BeerValue map, BeerValue key);

/* ------------------------------------------------------------------ */
/* Reference counting (advanced use)                                    */
/* ------------------------------------------------------------------ */

/* Retain/release heap-allocated values when storing them beyond the
 * current native call frame. beer_int/float/bool/nil never need this. */

/* Forward declarations (defined in memory.h / alloc.c) */
void object_retain(Value v);
void object_release(Value v);

static inline void beer_retain (BeerValue v) { if (is_pointer(v)) object_retain(v);  }
static inline void beer_release(BeerValue v) { if (is_pointer(v)) object_release(v); }

#ifdef __cplusplus
}
#endif

#endif /* BEER_H */
