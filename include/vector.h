/* Vector - persistent vector (32-way bit-partitioned trie + tail)
 *
 * Two kinds of operations:
 *
 *   Persistent (vector_conj, vector_assoc_n, vector_pop_persistent, and all
 *   functions returning a new Value): never modify their input; unchanged
 *   structure is shared with the result.
 *
 *   In-place (vector_push, vector_set, vector_pop, vector_clear,
 *   vector_reserve): ONLY for a vector you created and have not yet
 *   published (stored in a map/var, returned to beerlang, retained
 *   elsewhere). They are amortized O(1) for building. Never call them on a
 *   vector you received from beerlang or got as a borrowed reference
 *   (e.g. from hashmap_get) -- use the persistent operations instead.
 */

#ifndef BEERLANG_VECTOR_H
#define BEERLANG_VECTOR_H

#include <stddef.h>
#include <stdbool.h>
#include "value.h"

/* Vector initialization */
void vector_init(void);

/* Vector creation and destruction */
Value vector_create(size_t capacity);
Value vector_from_array(const Value* values, size_t count);

/* Vector properties */
size_t vector_length(Value vec);
/* Elements storable before the next allocation (tree size + tail capacity) */
size_t vector_capacity(Value vec);
bool vector_empty(Value vec);

/* Element access */
Value vector_get(Value vec, size_t index);
Value vector_first(Value vec);
Value vector_last(Value vec);

/* Persistent operations -- return a new vector, input unchanged */
Value vector_conj(Value vec, Value value);
/* index == length appends; index > length returns VALUE_NIL */
Value vector_assoc_n(Value vec, size_t index, Value value);
/* Returns VALUE_NIL for an empty vector */
Value vector_pop_persistent(Value vec);

/* In-place operations -- see the contract at the top of this file */
void vector_push(Value vec, Value value);
void vector_set(Value vec, size_t index, Value value);
/* Removes and returns the last element as an OWNED reference (caller
 * releases it); VALUE_NIL if empty */
Value vector_pop(Value vec);
void vector_clear(Value vec);
void vector_reserve(Value vec, size_t new_capacity);

/* Vector operations */
Value vector_slice(Value vec, size_t start, size_t end);
Value vector_concat(Value vec1, Value vec2);
/* O(1): shares all structure with the original */
Value vector_clone(Value vec);

/* Predicates */
bool is_vector(Value v);
bool vector_equal(Value a, Value b);

/* Higher-order functions */
typedef Value (*VectorMapFn)(Value elem);
typedef bool (*VectorFilterFn)(Value elem);
typedef Value (*VectorFoldFn)(Value acc, Value elem);

Value vector_map(Value vec, VectorMapFn fn);
Value vector_filter(Value vec, VectorFilterFn fn);
Value vector_fold(Value vec, Value init, VectorFoldFn fn);

/* Conversion to/from lists */
Value vector_from_list(Value list);
Value vector_to_list(Value vec);

/* Printing */
void vector_print(Value vec);
void vector_print_readable(Value vec);

#endif /* BEERLANG_VECTOR_H */
