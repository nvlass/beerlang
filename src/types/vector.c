/* Vector implementation - dynamic array */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "vector.h"
#include "beerlang.h"
#include "cons.h"

/* Default initial capacity */
#define DEFAULT_VECTOR_CAPACITY 8

/* Vector object layout */
typedef struct {
    struct Object header;
    size_t length;      /* Number of elements */
    size_t capacity;    /* Allocated capacity */
    Value* elements;    /* Dynamic array of values */
} Vector;

/* Destructor - release all elements */
static void vector_destructor(struct Object* obj) {
    Vector* vec = (Vector*)obj;

    /* Release all heap-allocated elements */
    for (size_t i = 0; i < vec->length; i++) {
        if (is_pointer(vec->elements[i])) {
            object_release(vec->elements[i]);
        }
    }

    /* Free the elements array */
    if (vec->elements) {
        free(vec->elements);
    }
}

/* Initialize vector type (idempotent - safe to call multiple times) */
static void vector_init_type(void) {
    object_register_destructor(TYPE_VECTOR, vector_destructor);
}

/* Public initialization function */
void vector_init(void) {
    vector_init_type();
}

/* Create a new vector with specified capacity */
Value vector_create(size_t capacity) {
    vector_init_type();

    if (capacity == 0) {
        capacity = DEFAULT_VECTOR_CAPACITY;
    }

    Vector* vec = (Vector*)object_alloc(TYPE_VECTOR, sizeof(Vector));
    Value obj = tag_pointer(vec);

    vec->length = 0;
    vec->capacity = capacity;
    vec->elements = (Value*)malloc(sizeof(Value) * capacity);

    if (!vec->elements) {
        fprintf(stderr, "FATAL: Out of memory allocating vector elements\n");
        abort();
    }

    return obj;
}

/* Create vector from array */
Value vector_from_array(const Value* values, size_t count) {
    Value vec = vector_create(count);
    Vector* v = (Vector*)untag_pointer(vec);

    for (size_t i = 0; i < count; i++) {
        v->elements[i] = values[i];
        /* Retain heap objects */
        if (is_pointer(values[i])) {
            object_retain(values[i]);
        }
    }

    v->length = count;
    return vec;
}

/* Check if value is a vector */
bool is_vector(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_VECTOR;
}

/* Get vector length */
size_t vector_length(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);
    return v->length;
}

/* Get vector capacity */
size_t vector_capacity(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);
    return v->capacity;
}

/* Check if vector is empty */
bool vector_empty(Value vec) {
    return vector_length(vec) == 0;
}

/* Resize vector capacity */
void vector_reserve(Value vec, size_t new_capacity) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    if (new_capacity <= v->capacity) {
        return;  /* Already have enough capacity */
    }

    Value* new_elements = (Value*)realloc(v->elements, sizeof(Value) * new_capacity);
    if (!new_elements) {
        fprintf(stderr, "FATAL: Out of memory resizing vector\n");
        abort();
    }

    v->elements = new_elements;
    v->capacity = new_capacity;
}

/* Get element at index */
Value vector_get(Value vec, size_t index) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    if (index >= v->length) {
        return VALUE_NIL;  /* Out of bounds */
    }

    return v->elements[index];
}

/* Set element at index */
void vector_set(Value vec, size_t index, Value value) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    if (index >= v->length) {
        return;  /* Out of bounds - no-op */
    }

    /* Retain new value */
    if (is_pointer(value)) {
        object_retain(value);
    }

    /* Release old value */
    if (is_pointer(v->elements[index])) {
        object_release(v->elements[index]);
    }

    v->elements[index] = value;
}

/* Get first element */
Value vector_first(Value vec) {
    return vector_get(vec, 0);
}

/* Get last element */
Value vector_last(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    if (v->length == 0) {
        return VALUE_NIL;
    }

    return v->elements[v->length - 1];
}

/* Push element to end */
void vector_push(Value vec, Value value) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    /* Resize if needed */
    if (v->length >= v->capacity) {
        size_t new_capacity = v->capacity * 2;
        if (new_capacity < DEFAULT_VECTOR_CAPACITY) {
            new_capacity = DEFAULT_VECTOR_CAPACITY;
        }
        vector_reserve(vec, new_capacity);
        /* Re-get pointer after potential realloc */
        v = (Vector*)untag_pointer(vec);
    }

    /* Add element */
    v->elements[v->length] = value;

    /* Retain heap objects */
    if (is_pointer(value)) {
        object_retain(value);
    }

    v->length++;
}

/* Pop element from end */
Value vector_pop(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    if (v->length == 0) {
        return VALUE_NIL;
    }

    v->length--;
    Value value = v->elements[v->length];

    /* Release heap objects */
    if (is_pointer(value)) {
        object_release(value);
    }

    return value;
}

/* Clear all elements */
void vector_clear(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    /* Release all elements */
    for (size_t i = 0; i < v->length; i++) {
        if (is_pointer(v->elements[i])) {
            object_release(v->elements[i]);
        }
    }

    v->length = 0;
}

/* Create a slice of the vector */
Value vector_slice(Value vec, size_t start, size_t end) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    /* Clamp to bounds */
    if (start > v->length) start = v->length;
    if (end > v->length) end = v->length;
    if (start > end) start = end;

    size_t slice_len = end - start;
    Value result = vector_create(slice_len);
    Vector* r = (Vector*)untag_pointer(result);

    for (size_t i = 0; i < slice_len; i++) {
        r->elements[i] = v->elements[start + i];
        if (is_pointer(r->elements[i])) {
            object_retain(r->elements[i]);
        }
    }

    r->length = slice_len;
    return result;
}

/* Concatenate two vectors */
Value vector_concat(Value vec1, Value vec2) {
    assert(is_vector(vec1));
    assert(is_vector(vec2));

    Vector* v1 = (Vector*)untag_pointer(vec1);
    Vector* v2 = (Vector*)untag_pointer(vec2);

    Value result = vector_create(v1->length + v2->length);
    Vector* r = (Vector*)untag_pointer(result);

    /* Copy first vector */
    for (size_t i = 0; i < v1->length; i++) {
        r->elements[i] = v1->elements[i];
        if (is_pointer(r->elements[i])) {
            object_retain(r->elements[i]);
        }
    }

    /* Copy second vector */
    for (size_t i = 0; i < v2->length; i++) {
        r->elements[v1->length + i] = v2->elements[i];
        if (is_pointer(r->elements[v1->length + i])) {
            object_retain(r->elements[v1->length + i]);
        }
    }

    r->length = v1->length + v2->length;
    return result;
}

/* Clone a vector */
Value vector_clone(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);
    return vector_from_array(v->elements, v->length);
}

/* Check equality */
bool vector_equal(Value a, Value b) {
    /* Fast path: same pointer */
    if (value_identical(a, b)) {
        return true;
    }

    /* Both must be vectors */
    if (!is_vector(a) || !is_vector(b)) {
        return false;
    }

    Vector* va = (Vector*)untag_pointer(a);
    Vector* vb = (Vector*)untag_pointer(b);

    /* Must have same length */
    if (va->length != vb->length) {
        return false;
    }

    /* Compare elements */
    for (size_t i = 0; i < va->length; i++) {
        if (!value_equal(va->elements[i], vb->elements[i])) {
            return false;
        }
    }

    return true;
}

/* Map function over vector */
Value vector_map(Value vec, VectorMapFn fn) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    Value result = vector_create(v->length);
    Vector* r = (Vector*)untag_pointer(result);

    for (size_t i = 0; i < v->length; i++) {
        Value mapped = fn(v->elements[i]);
        r->elements[i] = mapped;
        if (is_pointer(mapped)) {
            object_retain(mapped);
        }
    }

    r->length = v->length;
    return result;
}

/* Filter vector */
Value vector_filter(Value vec, VectorFilterFn fn) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    Value result = vector_create(v->length);
    Vector* r = (Vector*)untag_pointer(result);

    for (size_t i = 0; i < v->length; i++) {
        if (fn(v->elements[i])) {
            r->elements[r->length] = v->elements[i];
            if (is_pointer(v->elements[i])) {
                object_retain(v->elements[i]);
            }
            r->length++;
        }
    }

    return result;
}

/* Fold/reduce vector */
Value vector_fold(Value vec, Value init, VectorFoldFn fn) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    Value acc = init;
    for (size_t i = 0; i < v->length; i++) {
        acc = fn(acc, v->elements[i]);
    }

    return acc;
}

/* Convert list to vector */
Value vector_from_list(Value list) {
    /* Count list length */
    int64_t len = list_length(list);
    if (len < 0) {
        return VALUE_NIL;  /* Not a proper list */
    }

    Value vec = vector_create((size_t)len);
    Vector* v = (Vector*)untag_pointer(vec);

    /* Copy elements */
    Value current = list;
    size_t i = 0;
    while (is_cons(current)) {
        v->elements[i] = car(current);
        if (is_pointer(v->elements[i])) {
            object_retain(v->elements[i]);
        }
        current = cdr(current);
        i++;
    }

    v->length = (size_t)len;
    return vec;
}

/* Convert vector to list */
Value vector_to_list(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    return list_from_array(v->elements, v->length);
}

/* Print vector */
void vector_print(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    printf("[");
    for (size_t i = 0; i < v->length; i++) {
        if (i > 0) {
            printf(" ");
        }
        value_print(v->elements[i]);
    }
    printf("]");
}

/* Print vector in readable mode */
void vector_print_readable(Value vec) {
    assert(is_vector(vec));
    Vector* v = (Vector*)untag_pointer(vec);

    printf("[");
    for (size_t i = 0; i < v->length; i++) {
        if (i > 0) {
            printf(" ");
        }
        value_print_readable(v->elements[i]);
    }
    printf("]");
}
