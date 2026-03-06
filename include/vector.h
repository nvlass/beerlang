/* Vector - Dynamic array with random access */

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
size_t vector_capacity(Value vec);
bool vector_empty(Value vec);

/* Element access */
Value vector_get(Value vec, size_t index);
void vector_set(Value vec, size_t index, Value value);
Value vector_first(Value vec);
Value vector_last(Value vec);

/* Modification operations */
void vector_push(Value vec, Value value);
Value vector_pop(Value vec);
void vector_clear(Value vec);
void vector_reserve(Value vec, size_t new_capacity);

/* Vector operations */
Value vector_slice(Value vec, size_t start, size_t end);
Value vector_concat(Value vec1, Value vec2);
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
