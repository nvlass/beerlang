/* HashSet implementation - a wrapper around a persistent hashmap */

#include <stdio.h>
#include "beerlang.h"
#include "set.h"

typedef struct {
    struct Object header;
    Value map;   /* element -> element */
} HashSet;

static void set_destructor(struct Object* obj) {
    object_release(((HashSet*)obj)->map);
}

/* Takes ownership of `map` */
static Value set_wrap(Value map) {
    object_register_destructor(TYPE_SET, set_destructor);
    HashSet* s = (HashSet*)object_alloc(TYPE_SET, sizeof(HashSet));
    s->map = map;
    return tag_pointer(s);
}

static inline Value set_map(Value s) {
    return ((HashSet*)untag_pointer(s))->map;
}

Value set_create(void) {
    return set_wrap(hashmap_create_default());
}

Value set_from_array(const Value* xs, size_t n) {
    Value map = hashmap_create_default();
    /* Mutating is fine: the map is fresh and not yet shared */
    for (size_t i = 0; i < n; i++) {
        hashmap_set(map, xs[i], xs[i]);
    }
    return set_wrap(map);
}

size_t set_count(Value s) {
    return hashmap_size(set_map(s));
}

bool set_contains(Value s, Value x) {
    return hashmap_contains(set_map(s), x);
}

Value set_get(Value s, Value x) {
    return hashmap_get(set_map(s), x);
}

Value set_conj(Value s, Value x) {
    if (set_contains(s, x)) {
        object_retain(s);
        return s;
    }
    return set_wrap(hashmap_assoc(set_map(s), x, x));
}

Value set_disj(Value s, Value x) {
    if (!set_contains(s, x)) {
        object_retain(s);
        return s;
    }
    return set_wrap(hashmap_dissoc(set_map(s), x));
}

Value set_elements(Value s) {
    return hashmap_keys(set_map(s));
}

bool set_equal(Value a, Value b) {
    if (value_identical(a, b)) return true;
    if (!is_set(a) || !is_set(b)) return false;
    if (set_count(a) != set_count(b)) return false;
    Value elems = set_elements(a);
    size_t n = vector_length(elems);
    bool equal = true;
    for (size_t i = 0; i < n && equal; i++) {
        equal = set_contains(b, vector_get(elems, i));
    }
    object_release(elems);
    return equal;
}

/* Order-independent, so equal sets hash alike regardless of layout */
uint32_t set_hash(Value s) {
    Value elems = set_elements(s);
    size_t n = vector_length(elems);
    uint32_t h = 0x5e7u;
    for (size_t i = 0; i < n; i++) {
        h += value_hash(vector_get(elems, i));
    }
    object_release(elems);
    return h;
}

static void set_print_impl(Value s, bool readable) {
    Value elems = set_elements(s);
    size_t n = vector_length(elems);
    FILE* out = readable ? PR_OUT : stdout;
    fprintf(out, "#{");
    for (size_t i = 0; i < n; i++) {
        if (i > 0) fprintf(out, " ");
        if (readable) {
            value_print_readable(vector_get(elems, i));
        } else {
            value_print(vector_get(elems, i));
        }
    }
    fprintf(out, "}");
    object_release(elems);
}

void set_print(Value s)          { set_print_impl(s, false); }
void set_print_readable(Value s) { set_print_impl(s, true); }
