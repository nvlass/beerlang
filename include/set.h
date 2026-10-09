/* HashSet - persistent set, backed by the persistent HAMT hashmap
 * (each element is stored as key -> itself). */

#ifndef BEERLANG_SET_H
#define BEERLANG_SET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "value.h"

static inline bool is_set(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_SET;
}

Value set_create(void);
Value set_from_array(const Value* xs, size_t n);

size_t set_count(Value s);
bool   set_contains(Value s, Value x);
/* The stored element equal to x, or VALUE_NIL (borrowed reference) */
Value  set_get(Value s, Value x);

/* Persistent: return a new set, input unchanged */
Value set_conj(Value s, Value x);
Value set_disj(Value s, Value x);

/* New vector of the elements (owned) */
Value set_elements(Value s);

bool     set_equal(Value a, Value b);
uint32_t set_hash(Value s);

void set_print(Value s);
void set_print_readable(Value s);

#endif /* BEERLANG_SET_H */
