/* Vector implementation - persistent vector
 *
 * Clojure-style 32-way bit-partitioned trie plus a tail buffer. Elements
 * at index < tailoff(cnt) live in the tree under `root`; the rest live in
 * `tail`. Leaves, internal nodes and the tail are all TYPE_VEC_NODE: a
 * header followed by Value slots. A node's capacity comes from header.size
 * (object_alloc records the requested size), so tails can be smaller than
 * 32 slots while tree leaves and internal nodes are always exactly 32.
 *
 * Nodes are shared between vectors via refcounting, never exposed to
 * beerlang, and always referenced by a counted pointer from a wrapper or
 * a parent node.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "vector.h"
#include "beerlang.h"
#include "cons.h"

#define VEC_BITS  5
#define VEC_WIDTH 32
#define VEC_MASK  31
#define MIN_TAIL_CAP 8

typedef struct {
    struct Object header;
    Value slots[];
} VecNode;

typedef struct {
    struct Object header;
    uint32_t cnt;
    uint32_t shift;
    Value root;   /* VecNode, or VALUE_NIL while cnt <= 32 */
    Value tail;   /* VecNode, or VALUE_NIL while cnt == 0 */
} Vector;

static inline Vector*  as_vec(Value v)  { return (Vector*)untag_pointer(v); }
static inline VecNode* as_node(Value v) { return (VecNode*)untag_pointer(v); }

static inline uint32_t refcount_of(Value v) {
    return ((struct Object*)untag_pointer(v))->refcount;
}

static inline size_t node_cap(Value node) {
    return (as_node(node)->header.size - sizeof(VecNode)) / sizeof(Value);
}

static inline void retain_if_ptr(Value v)  { if (is_pointer(v)) object_retain(v); }
static inline void release_if_ptr(Value v) { if (is_pointer(v)) object_release(v); }

static inline size_t tailoff(size_t cnt) {
    return cnt < VEC_WIDTH ? 0 : ((cnt - 1) >> VEC_BITS) << VEC_BITS;
}

/* ----------------------------------------------------------------
 * Allocation and destruction
 * ---------------------------------------------------------------- */

static void vec_node_destructor(struct Object* obj) {
    VecNode* n = (VecNode*)obj;
    size_t cap = (n->header.size - sizeof(VecNode)) / sizeof(Value);
    for (size_t i = 0; i < cap; i++) {
        release_if_ptr(n->slots[i]);
    }
}

static void vector_destructor(struct Object* obj) {
    Vector* v = (Vector*)obj;
    release_if_ptr(v->root);
    release_if_ptr(v->tail);
}

static void vector_init_type(void) {
    object_register_destructor(TYPE_VECTOR, vector_destructor);
    object_register_destructor(TYPE_VEC_NODE, vec_node_destructor);
}

void vector_init(void) {
    vector_init_type();
}

/* object_alloc is calloc, and VALUE_NIL is all-zero, so slots start nil */
static Value node_alloc(size_t cap) {
    VecNode* n = (VecNode*)object_alloc(TYPE_VEC_NODE, sizeof(VecNode) + cap * sizeof(Value));
    return tag_pointer(n);
}

static Value node_clone(Value node) {
    size_t cap = node_cap(node);
    Value copy = node_alloc(cap);
    VecNode* src = as_node(node);
    VecNode* dst = as_node(copy);
    for (size_t i = 0; i < cap; i++) {
        dst->slots[i] = src->slots[i];
        retain_if_ptr(dst->slots[i]);
    }
    return copy;
}

static Value vec_alloc(void) {
    vector_init_type();
    Vector* v = (Vector*)object_alloc(TYPE_VECTOR, sizeof(Vector));
    v->shift = VEC_BITS;
    return tag_pointer(v);
}

/* Builds a chain of fresh internal nodes from `level` down to `node`,
 * transferring the caller's reference to `node` into the chain. */
static Value new_path(uint32_t level, Value node) {
    if (level == 0) return node;
    Value r = node_alloc(VEC_WIDTH);
    as_node(r)->slots[0] = new_path(level - VEC_BITS, node);
    return r;
}

/* ----------------------------------------------------------------
 * Read access
 * ---------------------------------------------------------------- */

/* The slot array holding index i, and the index of its first element. */
static const Value* chunk_for(Vector* v, size_t i, size_t* base) {
    size_t to = tailoff(v->cnt);
    if (i >= to) {
        *base = to;
        return as_node(v->tail)->slots;
    }
    Value node = v->root;
    for (uint32_t level = v->shift; level > 0; level -= VEC_BITS) {
        node = as_node(node)->slots[(i >> level) & VEC_MASK];
    }
    *base = i & ~(size_t)VEC_MASK;
    return as_node(node)->slots;
}

typedef struct {
    Vector* v;
    size_t i, base, end;
    const Value* arr;
} VecIter;

static inline void vec_iter_init(VecIter* it, Vector* v, size_t start) {
    it->v = v;
    it->i = start;
    it->base = 0;
    it->end = 0;
    it->arr = NULL;
}

static inline bool vec_iter_next(VecIter* it, Value* out) {
    if (it->i >= it->v->cnt) return false;
    if (it->i >= it->end || it->arr == NULL) {
        it->arr = chunk_for(it->v, it->i, &it->base);
        it->end = it->base + VEC_WIDTH;
        if (it->end > it->v->cnt) it->end = it->v->cnt;
    }
    *out = it->arr[it->i - it->base];
    it->i++;
    return true;
}

bool is_vector(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_VECTOR;
}

size_t vector_length(Value vec) {
    assert(is_vector(vec));
    return as_vec(vec)->cnt;
}

size_t vector_capacity(Value vec) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    return tailoff(v->cnt) + (is_nil(v->tail) ? 0 : node_cap(v->tail));
}

bool vector_empty(Value vec) {
    return vector_length(vec) == 0;
}

Value vector_get(Value vec, size_t index) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    if (index >= v->cnt) return VALUE_NIL;
    size_t base;
    const Value* arr = chunk_for(v, index, &base);
    return arr[index - base];
}

Value vector_first(Value vec) {
    return vector_get(vec, 0);
}

Value vector_last(Value vec) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    return v->cnt == 0 ? VALUE_NIL : vector_get(vec, v->cnt - 1);
}

/* The tree leaf holding index i (i < tailoff). */
static Value leaf_for(Vector* v, size_t i) {
    Value node = v->root;
    for (uint32_t level = v->shift; level > 0; level -= VEC_BITS) {
        node = as_node(node)->slots[(i >> level) & VEC_MASK];
    }
    return node;
}

/* ----------------------------------------------------------------
 * Persistent operations
 * ---------------------------------------------------------------- */

static Value push_tail_persistent(size_t cnt, uint32_t level, Value parent, Value tailnode) {
    Value ret = is_nil(parent) ? node_alloc(VEC_WIDTH) : node_clone(parent);
    VecNode* p = as_node(ret);
    size_t sub = ((cnt - 1) >> level) & VEC_MASK;
    Value insert;
    if (level == VEC_BITS) {
        insert = tailnode;
    } else {
        Value child = p->slots[sub];
        insert = is_nil(child)
            ? new_path(level - VEC_BITS, tailnode)
            : push_tail_persistent(cnt, level - VEC_BITS, child, tailnode);
    }
    release_if_ptr(p->slots[sub]);
    p->slots[sub] = insert;
    return ret;
}

Value vector_conj(Value vec, Value value) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    size_t cnt = v->cnt;
    size_t len = cnt - tailoff(cnt);

    Value result = vec_alloc();
    Vector* r = as_vec(result);
    retain_if_ptr(value);

    if (len < VEC_WIDTH) {
        Value nt = node_alloc(len + 1);
        VecNode* t = as_node(nt);
        if (len > 0) {
            VecNode* old = as_node(v->tail);
            for (size_t i = 0; i < len; i++) {
                t->slots[i] = old->slots[i];
                retain_if_ptr(t->slots[i]);
            }
        }
        t->slots[len] = value;
        r->root = v->root;
        retain_if_ptr(r->root);
        r->shift = v->shift;
        r->tail = nt;
        r->cnt = (uint32_t)(cnt + 1);
        return result;
    }

    /* Full tail: it becomes a tree leaf shared with the original. */
    Value tailnode = v->tail;
    object_retain(tailnode);
    if ((cnt >> VEC_BITS) > ((size_t)1 << v->shift)) {
        Value nr = node_alloc(VEC_WIDTH);
        as_node(nr)->slots[0] = v->root;
        retain_if_ptr(v->root);
        as_node(nr)->slots[1] = new_path(v->shift, tailnode);
        r->root = nr;
        r->shift = v->shift + VEC_BITS;
    } else {
        r->root = push_tail_persistent(cnt, v->shift, v->root, tailnode);
        r->shift = v->shift;
    }
    Value nt = node_alloc(1);
    as_node(nt)->slots[0] = value;
    r->tail = nt;
    r->cnt = (uint32_t)(cnt + 1);
    return result;
}

static Value do_assoc(uint32_t level, Value node, size_t i, Value value) {
    Value ret = node_clone(node);
    VecNode* p = as_node(ret);
    if (level == 0) {
        release_if_ptr(p->slots[i & VEC_MASK]);
        p->slots[i & VEC_MASK] = value;
    } else {
        size_t sub = (i >> level) & VEC_MASK;
        Value nc = do_assoc(level - VEC_BITS, p->slots[sub], i, value);
        release_if_ptr(p->slots[sub]);
        p->slots[sub] = nc;
    }
    return ret;
}

Value vector_assoc_n(Value vec, size_t index, Value value) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    size_t cnt = v->cnt;
    if (index == cnt) return vector_conj(vec, value);
    if (index > cnt) return VALUE_NIL;

    Value result = vec_alloc();
    Vector* r = as_vec(result);
    r->cnt = v->cnt;
    r->shift = v->shift;
    retain_if_ptr(value);

    size_t to = tailoff(cnt);
    if (index >= to) {
        Value nt = node_clone(v->tail);
        VecNode* t = as_node(nt);
        release_if_ptr(t->slots[index - to]);
        t->slots[index - to] = value;
        r->tail = nt;
        r->root = v->root;
        retain_if_ptr(r->root);
    } else {
        r->root = do_assoc(v->shift, v->root, index, value);
        r->tail = v->tail;
        object_retain(r->tail);
    }
    return result;
}

/* Returns the tree with its last leaf removed, or VALUE_NIL if that
 * leaves the subtree empty. */
static Value pop_tail_persistent(size_t cnt, uint32_t level, Value node) {
    size_t sub = ((cnt - 2) >> level) & VEC_MASK;
    if (level > VEC_BITS) {
        Value nc = pop_tail_persistent(cnt, level - VEC_BITS, as_node(node)->slots[sub]);
        if (is_nil(nc) && sub == 0) return VALUE_NIL;
        Value ret = node_clone(node);
        release_if_ptr(as_node(ret)->slots[sub]);
        as_node(ret)->slots[sub] = nc;
        return ret;
    }
    if (sub == 0) return VALUE_NIL;
    Value ret = node_clone(node);
    release_if_ptr(as_node(ret)->slots[sub]);
    as_node(ret)->slots[sub] = VALUE_NIL;
    return ret;
}

Value vector_pop_persistent(Value vec) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    size_t cnt = v->cnt;
    if (cnt == 0) return VALUE_NIL;

    Value result = vec_alloc();
    if (cnt == 1) return result;

    Vector* r = as_vec(result);
    size_t len = cnt - tailoff(cnt);

    if (len > 1) {
        Value nt = node_alloc(len - 1);
        VecNode* t = as_node(nt);
        VecNode* old = as_node(v->tail);
        for (size_t i = 0; i < len - 1; i++) {
            t->slots[i] = old->slots[i];
            retain_if_ptr(t->slots[i]);
        }
        r->root = v->root;
        retain_if_ptr(r->root);
        r->shift = v->shift;
        r->tail = nt;
        r->cnt = (uint32_t)(cnt - 1);
        return result;
    }

    /* The tail's only element goes; the tree's last leaf becomes the tail. */
    Value newtail = leaf_for(v, cnt - 2);
    object_retain(newtail);
    Value newroot = pop_tail_persistent(cnt, v->shift, v->root);
    uint32_t newshift = v->shift;
    if (newshift > VEC_BITS && !is_nil(newroot) && is_nil(as_node(newroot)->slots[1])) {
        Value only = as_node(newroot)->slots[0];
        object_retain(only);
        object_release(newroot);
        newroot = only;
        newshift -= VEC_BITS;
    }
    r->root = newroot;
    r->shift = newshift;
    r->tail = newtail;
    r->cnt = (uint32_t)(cnt - 1);
    return result;
}

/* ----------------------------------------------------------------
 * In-place operations (see the contract in vector.h)
 *
 * A node may be mutated in place only if the wrapper and every node on
 * the path from the root down to it have refcount 1. This is decided
 * top-down: a shared node is copied before descending into it, and
 * copying retains its children, so they are in turn seen as shared and
 * copied. Checking bottom-up would be unsound -- after vector_clone, an
 * interior node can have refcount 1 yet be reachable from two vectors
 * through their shared root.
 * ---------------------------------------------------------------- */

static void assert_exclusive(Value vec) {
    (void)vec;
    assert(refcount_of(vec) == 1 && "in-place vector op on a shared vector");
}

/* `slot` holds a counted reference to a node, inside a container that is
 * already exclusive. Afterwards the node in `slot` is exclusive too. */
static void make_editable(Value* slot) {
    if (refcount_of(*slot) == 1) return;
    Value copy = node_clone(*slot);
    object_release(*slot);
    *slot = copy;
}

/* Ensure the tail is exclusive with room for at least want_cap slots. */
static void tail_reserve(Vector* v, size_t len, size_t want_cap) {
    if (is_nil(v->tail)) {
        v->tail = node_alloc(want_cap);
        return;
    }
    size_t cap = node_cap(v->tail);
    bool exclusive = refcount_of(v->tail) == 1;
    if (exclusive && cap >= want_cap) return;

    Value nt = node_alloc(cap > want_cap ? cap : want_cap);
    VecNode* dst = as_node(nt);
    VecNode* src = as_node(v->tail);
    if (exclusive) {
        /* Move the slots: the old node gives up its references. */
        memcpy(dst->slots, src->slots, len * sizeof(Value));
        memset(src->slots, 0, len * sizeof(Value));
    } else {
        for (size_t i = 0; i < len; i++) {
            dst->slots[i] = src->slots[i];
            retain_if_ptr(dst->slots[i]);
        }
    }
    object_release(v->tail);
    v->tail = nt;
}

static void push_tail_mut(size_t cnt, uint32_t level, Value* parent_slot, Value tailnode) {
    if (is_nil(*parent_slot)) {
        *parent_slot = node_alloc(VEC_WIDTH);
    } else {
        make_editable(parent_slot);
    }
    VecNode* p = as_node(*parent_slot);
    size_t sub = ((cnt - 1) >> level) & VEC_MASK;
    if (level == VEC_BITS) {
        p->slots[sub] = tailnode;
        return;
    }
    if (is_nil(p->slots[sub])) {
        p->slots[sub] = new_path(level - VEC_BITS, tailnode);
    } else {
        push_tail_mut(cnt, level - VEC_BITS, &p->slots[sub], tailnode);
    }
}

void vector_push(Value vec, Value value) {
    assert(is_vector(vec));
    assert_exclusive(vec);
    Vector* v = as_vec(vec);
    size_t cnt = v->cnt;
    size_t len = cnt - tailoff(cnt);
    retain_if_ptr(value);

    if (len < VEC_WIDTH) {
        size_t cap = is_nil(v->tail) ? 0 : node_cap(v->tail);
        size_t want = cap;
        if (len >= cap) {
            want = cap * 2;
            if (want < MIN_TAIL_CAP) want = MIN_TAIL_CAP;
            if (want > VEC_WIDTH) want = VEC_WIDTH;
        }
        if (!is_nil(v->root)) want = VEC_WIDTH;
        if (want < len + 1) want = len + 1;
        tail_reserve(v, len, want);
        as_node(v->tail)->slots[len] = value;
        v->cnt++;
        return;
    }

    /* Full tail (always exactly 32 slots): move it into the tree. */
    Value tailnode = v->tail;
    assert(node_cap(tailnode) == VEC_WIDTH);
    if ((cnt >> VEC_BITS) > ((size_t)1 << v->shift)) {
        Value nr = node_alloc(VEC_WIDTH);
        as_node(nr)->slots[0] = v->root;
        as_node(nr)->slots[1] = new_path(v->shift, tailnode);
        v->root = nr;
        v->shift += VEC_BITS;
    } else {
        push_tail_mut(cnt, v->shift, &v->root, tailnode);
    }
    v->tail = node_alloc(VEC_WIDTH);
    as_node(v->tail)->slots[0] = value;
    v->cnt++;
}

/* Replace an exclusive wrapper's contents with a freshly built vector's. */
static void vec_swap_in(Value dst_val, Value fresh) {
    Vector* dst = as_vec(dst_val);
    Vector* src = as_vec(fresh);
    release_if_ptr(dst->root);
    release_if_ptr(dst->tail);
    dst->root = src->root;
    dst->tail = src->tail;
    dst->cnt = src->cnt;
    dst->shift = src->shift;
    src->root = VALUE_NIL;
    src->tail = VALUE_NIL;
    src->cnt = 0;
    object_release(fresh);
}

void vector_set(Value vec, size_t index, Value value) {
    assert(is_vector(vec));
    assert_exclusive(vec);
    if (index >= as_vec(vec)->cnt) return;
    vec_swap_in(vec, vector_assoc_n(vec, index, value));
}

Value vector_pop(Value vec) {
    assert(is_vector(vec));
    assert_exclusive(vec);
    Vector* v = as_vec(vec);
    if (v->cnt == 0) return VALUE_NIL;
    Value last = vector_get(vec, v->cnt - 1);
    retain_if_ptr(last);
    vec_swap_in(vec, vector_pop_persistent(vec));
    return last;
}

void vector_clear(Value vec) {
    assert(is_vector(vec));
    assert_exclusive(vec);
    Vector* v = as_vec(vec);
    release_if_ptr(v->root);
    release_if_ptr(v->tail);
    v->root = VALUE_NIL;
    v->tail = VALUE_NIL;
    v->cnt = 0;
    v->shift = VEC_BITS;
}

void vector_reserve(Value vec, size_t new_capacity) {
    assert(is_vector(vec));
    assert_exclusive(vec);
    Vector* v = as_vec(vec);
    if (new_capacity <= vector_capacity(vec)) return;
    size_t to = tailoff(v->cnt);
    size_t want = new_capacity - to;
    if (want > VEC_WIDTH) want = VEC_WIDTH;
    tail_reserve(v, v->cnt - to, want);
}

/* ----------------------------------------------------------------
 * Construction and derived operations
 * ---------------------------------------------------------------- */

Value vector_create(size_t capacity) {
    Value vec = vec_alloc();
    if (capacity > 0) {
        as_vec(vec)->tail = node_alloc(capacity < VEC_WIDTH ? capacity : VEC_WIDTH);
    }
    return vec;
}

Value vector_from_array(const Value* values, size_t count) {
    Value vec = vector_create(count);
    for (size_t i = 0; i < count; i++) {
        vector_push(vec, values[i]);
    }
    return vec;
}

Value vector_clone(Value vec) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    Value result = vec_alloc();
    Vector* r = as_vec(result);
    r->cnt = v->cnt;
    r->shift = v->shift;
    r->root = v->root;
    r->tail = v->tail;
    retain_if_ptr(r->root);
    retain_if_ptr(r->tail);
    return result;
}

Value vector_slice(Value vec, size_t start, size_t end) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    if (start > v->cnt) start = v->cnt;
    if (end > v->cnt) end = v->cnt;
    if (start > end) start = end;

    Value result = vector_create(end - start);
    VecIter it;
    vec_iter_init(&it, v, start);
    Value x;
    for (size_t i = start; i < end && vec_iter_next(&it, &x); i++) {
        vector_push(result, x);
    }
    return result;
}

Value vector_concat(Value vec1, Value vec2) {
    assert(is_vector(vec1));
    assert(is_vector(vec2));
    Value result = vector_create(vector_length(vec1) + vector_length(vec2));
    VecIter it;
    Value x;
    vec_iter_init(&it, as_vec(vec1), 0);
    while (vec_iter_next(&it, &x)) vector_push(result, x);
    vec_iter_init(&it, as_vec(vec2), 0);
    while (vec_iter_next(&it, &x)) vector_push(result, x);
    return result;
}

bool vector_equal(Value a, Value b) {
    if (value_identical(a, b)) return true;
    if (!is_vector(a) || !is_vector(b)) return false;
    Vector* va = as_vec(a);
    Vector* vb = as_vec(b);
    if (va->cnt != vb->cnt) return false;

    VecIter ia, ib;
    vec_iter_init(&ia, va, 0);
    vec_iter_init(&ib, vb, 0);
    Value xa, xb;
    while (vec_iter_next(&ia, &xa) && vec_iter_next(&ib, &xb)) {
        if (!value_equal(xa, xb)) return false;
    }
    return true;
}

Value vector_map(Value vec, VectorMapFn fn) {
    assert(is_vector(vec));
    Value result = vector_create(vector_length(vec));
    VecIter it;
    vec_iter_init(&it, as_vec(vec), 0);
    Value x;
    while (vec_iter_next(&it, &x)) {
        vector_push(result, fn(x));
    }
    return result;
}

Value vector_filter(Value vec, VectorFilterFn fn) {
    assert(is_vector(vec));
    Value result = vector_create(0);
    VecIter it;
    vec_iter_init(&it, as_vec(vec), 0);
    Value x;
    while (vec_iter_next(&it, &x)) {
        if (fn(x)) vector_push(result, x);
    }
    return result;
}

Value vector_fold(Value vec, Value init, VectorFoldFn fn) {
    assert(is_vector(vec));
    Value acc = init;
    VecIter it;
    vec_iter_init(&it, as_vec(vec), 0);
    Value x;
    while (vec_iter_next(&it, &x)) {
        acc = fn(acc, x);
    }
    return acc;
}

Value vector_from_list(Value list) {
    int64_t len = list_length(list);
    if (len < 0) return VALUE_NIL;
    Value vec = vector_create((size_t)len);
    for (Value cur = list; is_cons(cur); cur = cdr(cur)) {
        vector_push(vec, car(cur));
    }
    return vec;
}

Value vector_to_list(Value vec) {
    assert(is_vector(vec));
    Vector* v = as_vec(vec);
    if (v->cnt == 0) return list_from_array(NULL, 0);

    Value* tmp = malloc(v->cnt * sizeof(Value));
    VecIter it;
    vec_iter_init(&it, v, 0);
    size_t n = 0;
    Value x;
    while (vec_iter_next(&it, &x)) tmp[n++] = x;
    Value result = list_from_array(tmp, n);
    free(tmp);
    return result;
}

void vector_print(Value vec) {
    assert(is_vector(vec));
    VecIter it;
    vec_iter_init(&it, as_vec(vec), 0);
    Value x;
    bool first = true;
    printf("[");
    while (vec_iter_next(&it, &x)) {
        if (!first) printf(" ");
        value_print(x);
        first = false;
    }
    printf("]");
}

void vector_print_readable(Value vec) {
    assert(is_vector(vec));
    VecIter it;
    vec_iter_init(&it, as_vec(vec), 0);
    Value x;
    bool first = true;
    fprintf(PR_OUT, "[");
    while (vec_iter_next(&it, &x)) {
        if (!first) fprintf(PR_OUT, " ");
        value_print_readable(x);
        first = false;
    }
    fprintf(PR_OUT, "]");
}
