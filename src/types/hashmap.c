/* HashMap implementation - Persistent Hash Array Mapped Trie (HAMT)
 *
 * Uses CHAMP (Compressed Hash-Array Mapped Prefix-tree) layout with
 * dual bitmaps: datamap for inline key-value pairs, nodemap for
 * child-node pointers.
 *
 * Node body layout: [ kv0, kv1, ... | child0, child1, ... ]
 *   data entries at front: 2 * popcount(datamap) Values
 *   node pointers at end:  popcount(nodemap) Values
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "hashmap.h"
#include "beerlang.h"
#include "bstring.h"
#include "symbol.h"
#include "vector.h"

/* ================================================================
 * Bit manipulation helpers
 * ================================================================ */

static inline uint32_t bit(uint32_t index) {
    return 1u << index;
}

static inline uint32_t mask(uint32_t hash, uint32_t depth) {
    return (hash >> (depth * 5)) & 0x1F;
}

/* popcount - number of set bits */
static inline uint32_t popcnt(uint32_t x) {
    return (uint32_t)__builtin_popcount(x);
}

/* ================================================================
 * HAMT Node structures
 * ================================================================ */

typedef struct {
    struct Object header;
    uint32_t datamap;   /* bits for inline key-value pairs */
    uint32_t nodemap;   /* bits for child-node pointers */
    Value body[];       /* [kv-pairs... | node-ptrs...] */
} HamtNode;

typedef struct {
    struct Object header;
    uint32_t hash;
    uint32_t count;
    Value entries[];    /* [k0,v0, k1,v1, ...] */
} HamtCollision;

/* HashMap top-level wrapper */
typedef struct {
    struct Object header;
    uint32_t size;      /* cached entry count */
    Value root;         /* pointer to root HamtNode, or VALUE_NIL */
} HashMap;

/* ================================================================
 * Hash function
 * ================================================================ */

uint32_t value_hash(Value v) {
    if (is_fixnum(v)) {
        uint64_t h = (uint64_t)untag_fixnum(v);
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return (uint32_t)h;
    }
    if (is_float(v)) {
        double d = untag_float(v);
        uint64_t bits;
        memcpy(&bits, &d, sizeof(bits));
        bits ^= bits >> 33;
        bits *= 0xff51afd7ed558ccdULL;
        bits ^= bits >> 33;
        return (uint32_t)bits;
    }
    if (is_char(v))  return (uint32_t)untag_char(v) * 0x9e3779b9;
    if (is_nil(v))   return 0;
    if (is_true(v))  return 1;
    if (is_false(v)) return 2;
    if (!is_pointer(v)) return 0;

    uint8_t type = object_type(v);
    switch (type) {
        case TYPE_STRING:  return string_hash(v);
        case TYPE_SYMBOL:  return symbol_hash(v);
        case TYPE_KEYWORD: return keyword_hash(v);
        case TYPE_BIGINT: {
            char* str = bigint_to_string(v, 10);
            uint32_t h = 2166136261u;
            for (char* p = str; *p; p++) {
                h ^= (uint32_t)*p;
                h *= 16777619u;
            }
            free(str);
            return h;
        }
        case TYPE_VECTOR: {
            uint32_t h = 2166136261u;
            size_t len = vector_length(v);
            for (size_t i = 0; i < len; i++) {
                h ^= value_hash(vector_get(v, i));
                h *= 16777619u;
            }
            return h;
        }
        case TYPE_HASHMAP: {
            uint32_t h = 0;
            Value keys = hashmap_keys(v);
            size_t n = vector_length(keys);
            for (size_t i = 0; i < n; i++) {
                Value key = vector_get(keys, i);
                Value val = hashmap_get(v, key);
                h ^= (value_hash(key) * 31 + value_hash(val));
            }
            object_release(keys);
            return h;
        }
        default:
            return (uint32_t)((uintptr_t)untag_pointer(v) >> 4);
    }
}

/* ================================================================
 * HAMT Node allocation and helpers
 * ================================================================ */

static inline uint32_t hamt_data_count(HamtNode* n) {
    return popcnt(n->datamap);
}

static inline uint32_t hamt_node_count(HamtNode* n) {
    return popcnt(n->nodemap);
}

/* Allocate a HAMT node with the given number of data pairs and child nodes */
static Value hamt_node_alloc(uint32_t datamap, uint32_t nodemap) {
    uint32_t dc = popcnt(datamap);
    uint32_t nc = popcnt(nodemap);
    size_t body_size = (2 * dc + nc) * sizeof(Value);
    size_t total = sizeof(HamtNode) + body_size;
    HamtNode* node = (HamtNode*)object_alloc(TYPE_HAMT_NODE, total);
    Value v = tag_pointer(node);
    node->datamap = datamap;
    node->nodemap = nodemap;
    return v;
}

/* Allocate a collision node */
static Value hamt_collision_alloc(uint32_t hash, uint32_t count) {
    size_t body_size = 2 * count * sizeof(Value);
    size_t total = sizeof(HamtCollision) + body_size;
    HamtCollision* cn = (HamtCollision*)object_alloc(TYPE_HAMT_COLLISION, total);
    Value v = tag_pointer(cn);
    cn->hash = hash;
    cn->count = count;
    return v;
}

/* Data index: position in body[] for a given bit */
static inline uint32_t data_index(HamtNode* n, uint32_t b) {
    return popcnt(n->datamap & (b - 1));
}

/* Node index: position in body[] for a given bit (after data entries) */
static inline uint32_t node_index(HamtNode* n, uint32_t b) {
    return popcnt(n->nodemap & (b - 1));
}

/* Get inline key at data position */
static inline Value data_key(HamtNode* n, uint32_t pos) {
    return n->body[2 * pos];
}

/* Get inline value at data position */
static inline Value data_val(HamtNode* n, uint32_t pos) {
    return n->body[2 * pos + 1];
}

/* Get child node at node position */
static inline Value child_node(HamtNode* n, uint32_t pos) {
    uint32_t dc = hamt_data_count(n);
    return n->body[2 * dc + pos];
}

/* Retain a value if it's a pointer */
static inline void retain_if_ptr(Value v) {
    if (is_pointer(v)) object_retain(v);
}

/* Release a value if it's a pointer */
static inline void release_if_ptr(Value v) {
    if (is_pointer(v)) object_release(v);
}

/* ================================================================
 * HAMT Node destructors
 * ================================================================ */

static void hamt_node_destructor(struct Object* obj) {
    HamtNode* n = (HamtNode*)obj;
    uint32_t dc = popcnt(n->datamap);
    uint32_t nc = popcnt(n->nodemap);

    /* Release all inline key-value pairs */
    for (uint32_t i = 0; i < dc; i++) {
        release_if_ptr(n->body[2 * i]);
        release_if_ptr(n->body[2 * i + 1]);
    }
    /* Release all child nodes */
    for (uint32_t i = 0; i < nc; i++) {
        object_release(n->body[2 * dc + i]);
    }
}

static void hamt_collision_destructor(struct Object* obj) {
    HamtCollision* cn = (HamtCollision*)obj;
    for (uint32_t i = 0; i < cn->count; i++) {
        release_if_ptr(cn->entries[2 * i]);
        release_if_ptr(cn->entries[2 * i + 1]);
    }
}

static void hashmap_destructor(struct Object* obj) {
    HashMap* map = (HashMap*)obj;
    if (!is_nil(map->root)) {
        object_release(map->root);
    }
}

/* ================================================================
 * HAMT Get
 * ================================================================ */

static Value hamt_get(Value node_val, uint32_t hash, Value key, uint32_t depth) {
    if (is_nil(node_val)) return VALUE_NIL;

    uint8_t type = object_type(node_val);

    if (type == TYPE_HAMT_COLLISION) {
        HamtCollision* cn = (HamtCollision*)untag_pointer(node_val);
        for (uint32_t i = 0; i < cn->count; i++) {
            if (value_equal(cn->entries[2 * i], key)) {
                return cn->entries[2 * i + 1];
            }
        }
        return VALUE_NIL;
    }

    assert(type == TYPE_HAMT_NODE);
    HamtNode* n = (HamtNode*)untag_pointer(node_val);
    uint32_t idx = mask(hash, depth);
    uint32_t b = bit(idx);

    if (n->datamap & b) {
        uint32_t pos = data_index(n, b);
        if (value_equal(data_key(n, pos), key)) {
            return data_val(n, pos);
        }
        return VALUE_NIL;
    }

    if (n->nodemap & b) {
        uint32_t pos = node_index(n, b);
        return hamt_get(child_node(n, pos), hash, key, depth + 1);
    }

    return VALUE_NIL;
}

static bool hamt_contains(Value node_val, uint32_t hash, Value key, uint32_t depth) {
    if (is_nil(node_val)) return false;

    uint8_t type = object_type(node_val);

    if (type == TYPE_HAMT_COLLISION) {
        HamtCollision* cn = (HamtCollision*)untag_pointer(node_val);
        for (uint32_t i = 0; i < cn->count; i++) {
            if (value_equal(cn->entries[2 * i], key)) return true;
        }
        return false;
    }

    assert(type == TYPE_HAMT_NODE);
    HamtNode* n = (HamtNode*)untag_pointer(node_val);
    uint32_t idx = mask(hash, depth);
    uint32_t b = bit(idx);

    if (n->datamap & b) {
        uint32_t pos = data_index(n, b);
        return value_equal(data_key(n, pos), key);
    }

    if (n->nodemap & b) {
        uint32_t pos = node_index(n, b);
        return hamt_contains(child_node(n, pos), hash, key, depth + 1);
    }

    return false;
}

/* ================================================================
 * HAMT Assoc (path-copying insert/update)
 * ================================================================ */

/* Create a node with two entries that differ at the current depth */
static Value hamt_merge_two(uint32_t hash1, Value key1, Value val1,
                            uint32_t hash2, Value key2, Value val2,
                            uint32_t depth) {
    if (depth >= 7) {
        /* Hash collision — both have same hash */
        Value cn_val = hamt_collision_alloc(hash1, 2);
        HamtCollision* cn = (HamtCollision*)untag_pointer(cn_val);
        cn->entries[0] = key1; retain_if_ptr(key1);
        cn->entries[1] = val1; retain_if_ptr(val1);
        cn->entries[2] = key2; retain_if_ptr(key2);
        cn->entries[3] = val2; retain_if_ptr(val2);
        return cn_val;
    }

    uint32_t idx1 = mask(hash1, depth);
    uint32_t idx2 = mask(hash2, depth);

    if (idx1 == idx2) {
        /* Same index at this depth — create node with single sub-node */
        Value sub = hamt_merge_two(hash1, key1, val1, hash2, key2, val2, depth + 1);
        uint32_t b = bit(idx1);
        Value node_val = hamt_node_alloc(0, b);
        HamtNode* node = (HamtNode*)untag_pointer(node_val);
        node->body[0] = sub; /* already has refcount=1 from alloc */
        return node_val;
    }

    /* Different indices — create node with two inline entries */
    uint32_t b1 = bit(idx1);
    uint32_t b2 = bit(idx2);
    Value node_val = hamt_node_alloc(b1 | b2, 0);
    HamtNode* node = (HamtNode*)untag_pointer(node_val);

    /* Entries must be ordered by bit position */
    if (idx1 < idx2) {
        node->body[0] = key1; retain_if_ptr(key1);
        node->body[1] = val1; retain_if_ptr(val1);
        node->body[2] = key2; retain_if_ptr(key2);
        node->body[3] = val2; retain_if_ptr(val2);
    } else {
        node->body[0] = key2; retain_if_ptr(key2);
        node->body[1] = val2; retain_if_ptr(val2);
        node->body[2] = key1; retain_if_ptr(key1);
        node->body[3] = val1; retain_if_ptr(val1);
    }

    return node_val;
}

/* Returns {new_node, size_delta} — size_delta is 0 (update) or 1 (insert) */
typedef struct { Value node; int delta; } AssocResult;

static AssocResult hamt_collision_assoc(Value cn_val, Value key, Value val) {
    HamtCollision* cn = (HamtCollision*)untag_pointer(cn_val);

    /* Check if key exists */
    for (uint32_t i = 0; i < cn->count; i++) {
        if (value_equal(cn->entries[2 * i], key)) {
            /* Update: copy with new value */
            Value new_val = hamt_collision_alloc(cn->hash, cn->count);
            HamtCollision* new_cn = (HamtCollision*)untag_pointer(new_val);
            for (uint32_t j = 0; j < cn->count; j++) {
                new_cn->entries[2 * j] = cn->entries[2 * j];
                retain_if_ptr(cn->entries[2 * j]);
                if (j == i) {
                    new_cn->entries[2 * j + 1] = val;
                    retain_if_ptr(val);
                } else {
                    new_cn->entries[2 * j + 1] = cn->entries[2 * j + 1];
                    retain_if_ptr(cn->entries[2 * j + 1]);
                }
            }
            return (AssocResult){new_val, 0};
        }
    }

    /* Insert: copy with added entry */
    Value new_val = hamt_collision_alloc(cn->hash, cn->count + 1);
    HamtCollision* new_cn = (HamtCollision*)untag_pointer(new_val);
    for (uint32_t j = 0; j < cn->count; j++) {
        new_cn->entries[2 * j] = cn->entries[2 * j];
        retain_if_ptr(cn->entries[2 * j]);
        new_cn->entries[2 * j + 1] = cn->entries[2 * j + 1];
        retain_if_ptr(cn->entries[2 * j + 1]);
    }
    new_cn->entries[2 * cn->count] = key;
    retain_if_ptr(key);
    new_cn->entries[2 * cn->count + 1] = val;
    retain_if_ptr(val);
    return (AssocResult){new_val, 1};
}

static AssocResult hamt_assoc(Value node_val, uint32_t hash, Value key, Value val, uint32_t depth) {
    if (is_nil(node_val)) {
        /* Empty — create single-entry node */
        uint32_t idx = mask(hash, depth);
        uint32_t b = bit(idx);
        Value new_node = hamt_node_alloc(b, 0);
        HamtNode* n = (HamtNode*)untag_pointer(new_node);
        n->body[0] = key; retain_if_ptr(key);
        n->body[1] = val; retain_if_ptr(val);
        return (AssocResult){new_node, 1};
    }

    uint8_t type = object_type(node_val);

    if (type == TYPE_HAMT_COLLISION) {
        HamtCollision* cn = (HamtCollision*)untag_pointer(node_val);
        if (cn->hash == hash) {
            return hamt_collision_assoc(node_val, key, val);
        }
        /* Different hash — wrap collision in a branch node and insert */
        /* First, wrap the existing collision node */
        uint32_t cn_idx = mask(cn->hash, depth);
        uint32_t new_idx = mask(hash, depth);

        if (cn_idx == new_idx) {
            /* Same slot at this depth — need to go deeper */
            uint32_t b = bit(cn_idx);
            /* Recurse: treat collision node as a child at next depth */
            AssocResult sub = hamt_assoc(node_val, hash, key, val, depth + 1);
            Value new_node = hamt_node_alloc(0, b);
            HamtNode* n = (HamtNode*)untag_pointer(new_node);
            n->body[0] = sub.node;
            return (AssocResult){new_node, sub.delta};
        }

        /* Different slots — create node with collision as child and new entry inline */
        uint32_t b_cn = bit(cn_idx);
        uint32_t b_new = bit(new_idx);
        Value new_node = hamt_node_alloc(b_new, b_cn);
        HamtNode* n = (HamtNode*)untag_pointer(new_node);
        /* Data first, then nodes */
        n->body[0] = key; retain_if_ptr(key);
        n->body[1] = val; retain_if_ptr(val);
        n->body[2] = node_val; object_retain(node_val);
        return (AssocResult){new_node, 1};
    }

    assert(type == TYPE_HAMT_NODE);
    HamtNode* n = (HamtNode*)untag_pointer(node_val);
    uint32_t idx = mask(hash, depth);
    uint32_t b = bit(idx);
    uint32_t dc = hamt_data_count(n);
    uint32_t nc = hamt_node_count(n);

    if (n->datamap & b) {
        /* Bit is in datamap — inline entry exists here */
        uint32_t pos = data_index(n, b);
        Value existing_key = data_key(n, pos);
        Value existing_val = data_val(n, pos);

        if (value_equal(existing_key, key)) {
            /* Same key — update value (path copy) */
            if (value_identical(existing_val, val)) {
                /* Same value — return same node */
                object_retain(node_val);
                return (AssocResult){node_val, 0};
            }
            Value new_node = hamt_node_alloc(n->datamap, n->nodemap);
            HamtNode* nn = (HamtNode*)untag_pointer(new_node);
            /* Copy everything, retain all */
            for (uint32_t i = 0; i < 2 * dc + nc; i++) {
                nn->body[i] = n->body[i];
                retain_if_ptr(n->body[i]);
            }
            /* Replace the value */
            release_if_ptr(nn->body[2 * pos + 1]);
            nn->body[2 * pos + 1] = val;
            retain_if_ptr(val);
            return (AssocResult){new_node, 0};
        }

        /* Different key — push both down into a sub-node */
        uint32_t existing_hash = value_hash(existing_key);
        Value sub = hamt_merge_two(existing_hash, existing_key, existing_val,
                                   hash, key, val, depth + 1);

        /* New node: remove inline entry, add sub-node */
        uint32_t new_datamap = n->datamap & ~b;
        uint32_t new_nodemap = n->nodemap | b;
        uint32_t new_dc = popcnt(new_datamap);
        uint32_t new_nc = popcnt(new_nodemap);
        Value new_node = hamt_node_alloc(new_datamap, new_nodemap);
        HamtNode* nn = (HamtNode*)untag_pointer(new_node);

        /* Copy data entries, skipping the one at pos */
        uint32_t di = 0;
        for (uint32_t i = 0; i < dc; i++) {
            if (i == pos) continue;
            nn->body[2 * di] = n->body[2 * i];
            retain_if_ptr(n->body[2 * i]);
            nn->body[2 * di + 1] = n->body[2 * i + 1];
            retain_if_ptr(n->body[2 * i + 1]);
            di++;
        }

        /* Copy existing child nodes, inserting new sub-node at correct position */
        uint32_t new_node_pos = node_index(nn, b);
        uint32_t oi = 0;
        for (uint32_t i = 0; i < new_nc; i++) {
            if (i == new_node_pos) {
                nn->body[2 * new_dc + i] = sub; /* already refcount=1 */
            } else {
                nn->body[2 * new_dc + i] = n->body[2 * dc + oi];
                retain_if_ptr(n->body[2 * dc + oi]);
                oi++;
            }
        }

        return (AssocResult){new_node, 1};
    }

    if (n->nodemap & b) {
        /* Bit is in nodemap — recurse into child */
        uint32_t pos = node_index(n, b);
        Value old_child = child_node(n, pos);
        AssocResult sub = hamt_assoc(old_child, hash, key, val, depth + 1);

        /* Path copy: same layout, replace child */
        Value new_node = hamt_node_alloc(n->datamap, n->nodemap);
        HamtNode* nn = (HamtNode*)untag_pointer(new_node);
        for (uint32_t i = 0; i < 2 * dc + nc; i++) {
            nn->body[i] = n->body[i];
            retain_if_ptr(n->body[i]);
        }
        /* Replace old child with new */
        release_if_ptr(nn->body[2 * dc + pos]);
        nn->body[2 * dc + pos] = sub.node; /* sub.node already refcount=1 */
        return (AssocResult){new_node, sub.delta};
    }

    /* Bit absent — add new inline entry */
    uint32_t new_datamap = n->datamap | b;
    Value new_node = hamt_node_alloc(new_datamap, n->nodemap);
    HamtNode* nn = (HamtNode*)untag_pointer(new_node);
    uint32_t new_dc = popcnt(new_datamap);
    uint32_t insert_pos = data_index(nn, b);

    /* Copy data entries with new entry inserted */
    uint32_t si = 0;
    for (uint32_t i = 0; i < new_dc; i++) {
        if (i == insert_pos) {
            nn->body[2 * i] = key; retain_if_ptr(key);
            nn->body[2 * i + 1] = val; retain_if_ptr(val);
        } else {
            nn->body[2 * i] = n->body[2 * si];
            retain_if_ptr(n->body[2 * si]);
            nn->body[2 * i + 1] = n->body[2 * si + 1];
            retain_if_ptr(n->body[2 * si + 1]);
            si++;
        }
    }

    /* Copy child nodes */
    for (uint32_t i = 0; i < nc; i++) {
        nn->body[2 * new_dc + i] = n->body[2 * dc + i];
        retain_if_ptr(n->body[2 * dc + i]);
    }

    return (AssocResult){new_node, 1};
}

/* ================================================================
 * HAMT Dissoc (path-copying remove)
 * ================================================================ */

typedef struct { Value node; int delta; } DissocResult;

static DissocResult hamt_dissoc(Value node_val, uint32_t hash, Value key, uint32_t depth) {
    if (is_nil(node_val)) {
        return (DissocResult){VALUE_NIL, 0};
    }

    uint8_t type = object_type(node_val);

    if (type == TYPE_HAMT_COLLISION) {
        HamtCollision* cn = (HamtCollision*)untag_pointer(node_val);
        /* Find key */
        for (uint32_t i = 0; i < cn->count; i++) {
            if (value_equal(cn->entries[2 * i], key)) {
                if (cn->count == 1) {
                    /* Last entry — return nil */
                    return (DissocResult){VALUE_NIL, -1};
                }
                if (cn->count == 2) {
                    /* Two entries, removing one — could collapse to inline in parent
                     * For simplicity, return a 1-entry collision node;
                     * the parent will handle collapse */
                    Value new_cn = hamt_collision_alloc(cn->hash, 1);
                    HamtCollision* ncn = (HamtCollision*)untag_pointer(new_cn);
                    uint32_t other = (i == 0) ? 1 : 0;
                    ncn->entries[0] = cn->entries[2 * other];
                    retain_if_ptr(cn->entries[2 * other]);
                    ncn->entries[1] = cn->entries[2 * other + 1];
                    retain_if_ptr(cn->entries[2 * other + 1]);
                    return (DissocResult){new_cn, -1};
                }
                /* More than 2 — copy without entry i */
                Value new_cn = hamt_collision_alloc(cn->hash, cn->count - 1);
                HamtCollision* ncn = (HamtCollision*)untag_pointer(new_cn);
                uint32_t j = 0;
                for (uint32_t k = 0; k < cn->count; k++) {
                    if (k == i) continue;
                    ncn->entries[2 * j] = cn->entries[2 * k];
                    retain_if_ptr(cn->entries[2 * k]);
                    ncn->entries[2 * j + 1] = cn->entries[2 * k + 1];
                    retain_if_ptr(cn->entries[2 * k + 1]);
                    j++;
                }
                return (DissocResult){new_cn, -1};
            }
        }
        /* Key not found */
        object_retain(node_val);
        return (DissocResult){node_val, 0};
    }

    assert(type == TYPE_HAMT_NODE);
    HamtNode* n = (HamtNode*)untag_pointer(node_val);
    uint32_t idx = mask(hash, depth);
    uint32_t b = bit(idx);
    uint32_t dc = hamt_data_count(n);
    uint32_t nc = hamt_node_count(n);

    if (n->datamap & b) {
        uint32_t pos = data_index(n, b);
        if (!value_equal(data_key(n, pos), key)) {
            /* Key not found */
            object_retain(node_val);
            return (DissocResult){node_val, 0};
        }

        /* Remove inline entry */
        if (dc == 1 && nc == 0) {
            /* Node becomes empty */
            return (DissocResult){VALUE_NIL, -1};
        }

        uint32_t new_datamap = n->datamap & ~b;
        Value new_node = hamt_node_alloc(new_datamap, n->nodemap);
        HamtNode* nn = (HamtNode*)untag_pointer(new_node);
        uint32_t new_dc = popcnt(new_datamap);

        uint32_t di = 0;
        for (uint32_t i = 0; i < dc; i++) {
            if (i == pos) continue;
            nn->body[2 * di] = n->body[2 * i];
            retain_if_ptr(n->body[2 * i]);
            nn->body[2 * di + 1] = n->body[2 * i + 1];
            retain_if_ptr(n->body[2 * i + 1]);
            di++;
        }
        for (uint32_t i = 0; i < nc; i++) {
            nn->body[2 * new_dc + i] = n->body[2 * dc + i];
            retain_if_ptr(n->body[2 * dc + i]);
        }
        return (DissocResult){new_node, -1};
    }

    if (n->nodemap & b) {
        uint32_t pos = node_index(n, b);
        Value old_child = child_node(n, pos);
        DissocResult sub = hamt_dissoc(old_child, hash, key, depth + 1);

        if (sub.delta == 0) {
            /* Key wasn't found in subtree */
            object_release(sub.node);
            object_retain(node_val);
            return (DissocResult){node_val, 0};
        }

        if (is_nil(sub.node)) {
            /* Child became empty — remove from nodemap */
            if (dc == 0 && nc == 1) {
                return (DissocResult){VALUE_NIL, -1};
            }
            uint32_t new_nodemap = n->nodemap & ~b;
            uint32_t new_nc = popcnt(new_nodemap);
            Value new_node = hamt_node_alloc(n->datamap, new_nodemap);
            HamtNode* nn = (HamtNode*)untag_pointer(new_node);
            for (uint32_t i = 0; i < 2 * dc; i++) {
                nn->body[i] = n->body[i];
                retain_if_ptr(n->body[i]);
            }
            uint32_t ni = 0;
            for (uint32_t i = 0; i < nc; i++) {
                if (i == pos) continue;
                nn->body[2 * dc + ni] = n->body[2 * dc + i];
                retain_if_ptr(n->body[2 * dc + i]);
                ni++;
            }
            (void)new_nc;
            return (DissocResult){new_node, -1};
        }

        /* Child updated — check if we should inline a single-entry child */
        uint8_t sub_type = object_type(sub.node);
        if (sub_type == TYPE_HAMT_NODE) {
            HamtNode* sub_n = (HamtNode*)untag_pointer(sub.node);
            if (popcnt(sub_n->datamap) == 1 && popcnt(sub_n->nodemap) == 0) {
                /* Child has single inline entry — pull it up into this node */
                Value ik = sub_n->body[0];
                Value iv = sub_n->body[1];

                /* Replace nodemap bit with datamap bit */
                uint32_t new_datamap = n->datamap | b;
                uint32_t new_nodemap = n->nodemap & ~b;
                uint32_t new_dc = popcnt(new_datamap);
                uint32_t new_nc_val = popcnt(new_nodemap);

                Value new_node = hamt_node_alloc(new_datamap, new_nodemap);
                HamtNode* nn = (HamtNode*)untag_pointer(new_node);

                /* Insert data at correct position */
                uint32_t insert_pos = data_index(nn, b);
                uint32_t si = 0;
                for (uint32_t i = 0; i < new_dc; i++) {
                    if (i == insert_pos) {
                        nn->body[2 * i] = ik;
                        retain_if_ptr(ik);
                        nn->body[2 * i + 1] = iv;
                        retain_if_ptr(iv);
                    } else {
                        nn->body[2 * i] = n->body[2 * si];
                        retain_if_ptr(n->body[2 * si]);
                        nn->body[2 * i + 1] = n->body[2 * si + 1];
                        retain_if_ptr(n->body[2 * si + 1]);
                        si++;
                    }
                }
                /* Copy child nodes except the removed one */
                uint32_t ni = 0;
                for (uint32_t i = 0; i < nc; i++) {
                    if (i == pos) continue;
                    nn->body[2 * new_dc + ni] = n->body[2 * dc + i];
                    retain_if_ptr(n->body[2 * dc + i]);
                    ni++;
                }
                (void)new_nc_val;
                object_release(sub.node);
                return (DissocResult){new_node, -1};
            }
        }

        /* Normal case: path copy with updated child */
        Value new_node = hamt_node_alloc(n->datamap, n->nodemap);
        HamtNode* nn = (HamtNode*)untag_pointer(new_node);
        for (uint32_t i = 0; i < 2 * dc + nc; i++) {
            nn->body[i] = n->body[i];
            retain_if_ptr(n->body[i]);
        }
        release_if_ptr(nn->body[2 * dc + pos]);
        nn->body[2 * dc + pos] = sub.node;
        return (DissocResult){new_node, -1};
    }

    /* Bit not set — key not found */
    object_retain(node_val);
    return (DissocResult){node_val, 0};
}

/* ================================================================
 * HAMT Foreach (DFS traversal)
 * ================================================================ */

static void hamt_foreach(Value node_val, HashMapIterFn fn, void* ctx) {
    if (is_nil(node_val)) return;

    uint8_t type = object_type(node_val);

    if (type == TYPE_HAMT_COLLISION) {
        HamtCollision* cn = (HamtCollision*)untag_pointer(node_val);
        for (uint32_t i = 0; i < cn->count; i++) {
            fn(cn->entries[2 * i], cn->entries[2 * i + 1], ctx);
        }
        return;
    }

    assert(type == TYPE_HAMT_NODE);
    HamtNode* n = (HamtNode*)untag_pointer(node_val);
    uint32_t dc = hamt_data_count(n);
    uint32_t nc = hamt_node_count(n);

    /* Visit inline entries */
    for (uint32_t i = 0; i < dc; i++) {
        fn(n->body[2 * i], n->body[2 * i + 1], ctx);
    }

    /* Recurse into children */
    for (uint32_t i = 0; i < nc; i++) {
        hamt_foreach(n->body[2 * dc + i], fn, ctx);
    }
}

/* ================================================================
 * HashMap public API
 * ================================================================ */

static void hashmap_init_types(void) {
    object_register_destructor(TYPE_HASHMAP, hashmap_destructor);
    object_register_destructor(TYPE_HAMT_NODE, hamt_node_destructor);
    object_register_destructor(TYPE_HAMT_COLLISION, hamt_collision_destructor);
}

void hashmap_init(void) {
    hashmap_init_types();
}

Value hashmap_create_default(void) {
    hashmap_init_types();
    HashMap* map = (HashMap*)object_alloc(TYPE_HASHMAP, sizeof(HashMap));
    Value v = tag_pointer(map);
    map->size = 0;
    map->root = VALUE_NIL;
    return v;
}

bool is_hashmap(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_HASHMAP;
}

size_t hashmap_size(Value map) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    return m->size;
}

bool hashmap_empty(Value map) {
    return hashmap_size(map) == 0;
}

Value hashmap_get(Value map, Value key) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    if (m->size == 0) return VALUE_NIL;
    return hamt_get(m->root, value_hash(key), key, 0);
}

Value hashmap_get_default(Value map, Value key, Value default_value) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    if (m->size == 0) return default_value;
    if (!hamt_contains(m->root, value_hash(key), key, 0)) return default_value;
    return hamt_get(m->root, value_hash(key), key, 0);
}

bool hashmap_contains(Value map, Value key) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    if (m->size == 0) return false;
    return hamt_contains(m->root, value_hash(key), key, 0);
}

/* Persistent assoc — returns a NEW hashmap */
Value hashmap_assoc(Value map, Value key, Value value) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);

    AssocResult r = hamt_assoc(m->root, value_hash(key), key, value, 0);

    HashMap* new_map = (HashMap*)object_alloc(TYPE_HASHMAP, sizeof(HashMap));
    Value new_map_val = tag_pointer(new_map);
    new_map->size = m->size + r.delta;
    new_map->root = r.node; /* already refcount=1 from alloc */
    return new_map_val;
}

/* Persistent dissoc — returns a NEW hashmap */
Value hashmap_dissoc(Value map, Value key) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);

    if (m->size == 0) {
        object_retain(map);
        return map;
    }

    DissocResult r = hamt_dissoc(m->root, value_hash(key), key, 0);

    if (r.delta == 0) {
        /* Key not found — return same map */
        if (!is_nil(r.node)) object_release(r.node);
        object_retain(map);
        return map;
    }

    HashMap* new_map = (HashMap*)object_alloc(TYPE_HASHMAP, sizeof(HashMap));
    Value new_map_val = tag_pointer(new_map);
    new_map->size = m->size + r.delta;
    new_map->root = r.node;
    return new_map_val;
}

/* Mutating set — modifies map in place (for internal use by compiler/namespace) */
void hashmap_set(Value map, Value key, Value value) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);

    AssocResult r = hamt_assoc(m->root, value_hash(key), key, value, 0);
    if (!is_nil(m->root)) {
        object_release(m->root);
    }
    m->root = r.node;
    m->size += r.delta;
}

/* Mutating remove — modifies map in place (for internal use) */
void hashmap_remove(Value map, Value key) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);

    if (m->size == 0) return;

    DissocResult r = hamt_dissoc(m->root, value_hash(key), key, 0);
    if (r.delta == 0) {
        if (!is_nil(r.node)) object_release(r.node);
        return;
    }
    object_release(m->root);
    m->root = r.node;
    m->size += r.delta;
}

/* Create hashmap from vector of [k1, v1, k2, v2, ...] */
Value hashmap_from_vec(Value vec) {
    assert(is_vector(vec));
    size_t len = vector_length(vec);
    if (len % 2 != 0) return VALUE_NIL;

    Value map = hashmap_create_default();
    for (size_t i = 0; i < len; i += 2) {
        hashmap_set(map, vector_get(vec, i), vector_get(vec, i + 1));
    }
    return map;
}

/* Keys, values, entries — use foreach */

typedef struct { Value vec; } CollectCtx;

static void collect_keys(Value key, Value value, void* ctx) {
    (void)value;
    CollectCtx* c = (CollectCtx*)ctx;
    vector_push(c->vec, key);
}

static void collect_values(Value key, Value value, void* ctx) {
    (void)key;
    CollectCtx* c = (CollectCtx*)ctx;
    vector_push(c->vec, value);
}

static void collect_entries(Value key, Value value, void* ctx) {
    CollectCtx* c = (CollectCtx*)ctx;
    Value pair = vector_create(2);
    vector_push(pair, key);
    vector_push(pair, value);
    vector_push(c->vec, pair);
    object_release(pair);
}

Value hashmap_keys(Value map) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    Value vec = vector_create(m->size);
    CollectCtx ctx = { vec };
    hamt_foreach(m->root, collect_keys, &ctx);
    return vec;
}

Value hashmap_values(Value map) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    Value vec = vector_create(m->size);
    CollectCtx ctx = { vec };
    hamt_foreach(m->root, collect_values, &ctx);
    return vec;
}

Value hashmap_entries(Value map) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    Value vec = vector_create(m->size);
    CollectCtx ctx = { vec };
    hamt_foreach(m->root, collect_entries, &ctx);
    return vec;
}

void hashmap_foreach(Value map, HashMapIterFn fn, void* ctx) {
    assert(is_hashmap(map));
    HashMap* m = (HashMap*)untag_pointer(map);
    hamt_foreach(m->root, fn, ctx);
}

typedef struct { Value map; } MergeCtx;

static void merge_cb(Value key, Value value, void* ctx) {
    MergeCtx* mc = (MergeCtx*)ctx;
    hashmap_set(mc->map, key, value);
}

Value hashmap_merge(Value map1, Value map2) {
    assert(is_hashmap(map1));
    assert(is_hashmap(map2));

    Value result = hashmap_create_default();
    HashMap* m1 = (HashMap*)untag_pointer(map1);
    HashMap* r = (HashMap*)untag_pointer(result);
    if (!is_nil(m1->root)) {
        object_retain(m1->root);
        r->root = m1->root;
        r->size = m1->size;
    }

    MergeCtx mctx = { result };
    HashMap* m2 = (HashMap*)untag_pointer(map2);
    hamt_foreach(m2->root, merge_cb, &mctx);

    return result;
}

typedef struct { Value other; bool equal; } EqCtx;

static void eq_cb(Value key, Value value, void* ctx) {
    EqCtx* ec = (EqCtx*)ctx;
    if (!ec->equal) return;
    if (!hashmap_contains(ec->other, key)) {
        ec->equal = false;
        return;
    }
    Value other_val = hashmap_get(ec->other, key);
    if (!value_equal(value, other_val)) {
        ec->equal = false;
    }
}

bool hashmap_equal(Value a, Value b) {
    if (value_identical(a, b)) return true;
    if (!is_hashmap(a) || !is_hashmap(b)) return false;

    HashMap* ma = (HashMap*)untag_pointer(a);
    HashMap* mb = (HashMap*)untag_pointer(b);
    if (ma->size != mb->size) return false;

    EqCtx ectx = { b, true };
    hamt_foreach(ma->root, eq_cb, &ectx);
    return ectx.equal;
}

/* Printing */
typedef struct { bool first; } PrintCtx;

static void print_entry(Value key, Value value, void* ctx) {
    PrintCtx* pc = (PrintCtx*)ctx;
    if (!pc->first) printf(", ");
    pc->first = false;
    value_print(key);
    printf(" ");
    value_print(value);
}

static void print_entry_readable(Value key, Value value, void* ctx) {
    PrintCtx* pc = (PrintCtx*)ctx;
    if (!pc->first) printf(", ");
    pc->first = false;
    value_print_readable(key);
    printf(" ");
    value_print_readable(value);
}

void hashmap_print(Value map) {
    assert(is_hashmap(map));
    printf("{");
    PrintCtx ctx = { true };
    hashmap_foreach(map, print_entry, &ctx);
    printf("}");
}

void hashmap_print_readable(Value map) {
    assert(is_hashmap(map));
    printf("{");
    PrintCtx ctx = { true };
    hashmap_foreach(map, print_entry_readable, &ctx);
    printf("}");
}
