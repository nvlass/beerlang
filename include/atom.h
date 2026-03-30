/* Atom - Mutable reference type (Clojure-style)
 *
 * Atoms provide a way to manage shared, mutable state.
 * With cooperative scheduling, swap! is naturally atomic.
 */

#ifndef BEERLANG_ATOM_H
#define BEERLANG_ATOM_H

#include "value.h"

/* Forward declaration */
typedef struct VM VM;

/* Atom object structure */
typedef struct Atom {
    struct Object header;
    Value value;      /* the mutable reference */
    Value watches;    /* placeholder for future add-watch (VALUE_NIL for now) */
    Value validator;  /* placeholder for future set-validator! (VALUE_NIL for now) */
} Atom;

/* Create a new atom with the given initial value. Returns a tagged Value. */
Value atom_new(Value initial);

/* Get the current value of the atom (no extra retain). */
Value atom_deref(Value atom);

/* Set the atom to new_val, return new_val. */
Value atom_reset(Value atom, Value new_val);

/* Apply fn to current value (+ extra args), store result. Uses temp VM.
 * Returns the new value. */
Value atom_swap(VM* vm, Value atom, Value fn, int extra_argc, Value* extra_argv);

/* If current == old, set to new_val and return true; else return false. */
bool atom_compare_and_set(Value atom, Value old_val, Value new_val);

/* Initialize atom type (register destructor) */
void atom_init(void);

/* Type check */
static inline bool is_atom(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_ATOM;
}

#endif /* BEERLANG_ATOM_H */
