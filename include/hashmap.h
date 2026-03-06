/* HashMap - Persistent Hash Array Mapped Trie (HAMT) */

#ifndef BEERLANG_HASHMAP_H
#define BEERLANG_HASHMAP_H

#include <stddef.h>
#include <stdbool.h>
#include "value.h"

/* HashMap initialization */
void hashmap_init(void);

/* HashMap creation */
Value hashmap_create_default(void);

/* Create hashmap from vector of [k1, v1, k2, v2, ...] */
Value hashmap_from_vec(Value vec);

/* HashMap properties */
size_t hashmap_size(Value map);
bool hashmap_empty(Value map);

/* Lookup */
Value hashmap_get(Value map, Value key);
Value hashmap_get_default(Value map, Value key, Value default_value);
bool hashmap_contains(Value map, Value key);

/* Persistent operations (return new maps with structural sharing) */
Value hashmap_assoc(Value map, Value key, Value value);
Value hashmap_dissoc(Value map, Value key);

/* Mutating convenience (modifies map in place — for internal use) */
void hashmap_set(Value map, Value key, Value value);
void hashmap_remove(Value map, Value key);

/* Bulk operations */
Value hashmap_keys(Value map);      /* Returns vector of keys */
Value hashmap_values(Value map);    /* Returns vector of values */
Value hashmap_entries(Value map);   /* Returns vector of [key value] pairs */

/* HashMap operations */
Value hashmap_merge(Value map1, Value map2);

/* Predicates */
bool is_hashmap(Value v);
bool hashmap_equal(Value a, Value b);

/* Iteration */
typedef void (*HashMapIterFn)(Value key, Value value, void* ctx);
void hashmap_foreach(Value map, HashMapIterFn fn, void* ctx);

/* Hash functions for different types */
uint32_t value_hash(Value v);

/* Printing */
void hashmap_print(Value map);
void hashmap_print_readable(Value map);

#endif /* BEERLANG_HASHMAP_H */
