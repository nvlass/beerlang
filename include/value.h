/* Value representation - Tagged union struct (Lua TValue style)
 *
 * All values in Beerlang are 16-byte tagged union structs.
 * The tag is an explicit enum field; fixnums and chars are immediate.
 */

#ifndef BEERLANG_VALUE_H
#define BEERLANG_VALUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Value tag enum */
typedef enum {
    TAG_NIL = 0,
    TAG_TRUE,
    TAG_FALSE,
    TAG_FIXNUM,
    TAG_FLOAT,
    TAG_CHAR,
    TAG_OBJECT,
} ValueTag;

/* Forward declare struct Object */
struct Object;

/* Value is a 16-byte tagged union */
typedef struct Value {
    ValueTag tag;
    union {
        int64_t        fixnum;     /* TAG_FIXNUM */
        double         floatnum;   /* TAG_FLOAT */
        uint32_t       character;  /* TAG_CHAR */
        struct Object* object;     /* TAG_OBJECT - all heap types */
    } as;
} Value;

/* Special constants */
static const Value VALUE_NIL   = { .tag = TAG_NIL,   .as = { .fixnum = 0 } };
static const Value VALUE_TRUE  = { .tag = TAG_TRUE,  .as = { .fixnum = 0 } };
static const Value VALUE_FALSE = { .tag = TAG_FALSE, .as = { .fixnum = 0 } };

/* Value identity comparison (replaces == on old uint64_t Values) */
static inline bool value_identical(Value a, Value b) {
    if (a.tag != b.tag) return false;
    switch (a.tag) {
        case TAG_NIL:
        case TAG_TRUE:
        case TAG_FALSE:
            return true;
        case TAG_FIXNUM:
            return a.as.fixnum == b.as.fixnum;
        case TAG_FLOAT:
            return a.as.floatnum == b.as.floatnum;
        case TAG_CHAR:
            return a.as.character == b.as.character;
        case TAG_OBJECT:
            return a.as.object == b.as.object;
    }
    return false;
}

/* Tag predicates */
static inline uint8_t get_tag(Value v) {
    return (uint8_t)v.tag;
}

static inline bool is_pointer(Value v) {
    return v.tag == TAG_OBJECT;
}

static inline bool is_fixnum(Value v) {
    return v.tag == TAG_FIXNUM;
}

static inline bool is_char(Value v) {
    return v.tag == TAG_CHAR;
}

static inline bool is_special(Value v) {
    return v.tag == TAG_NIL || v.tag == TAG_TRUE || v.tag == TAG_FALSE;
}

/* Special value predicates */
static inline bool is_nil(Value v) {
    return v.tag == TAG_NIL;
}

static inline bool is_true(Value v) {
    return v.tag == TAG_TRUE;
}

static inline bool is_false(Value v) {
    return v.tag == TAG_FALSE;
}

static inline bool is_bool(Value v) {
    return v.tag == TAG_TRUE || v.tag == TAG_FALSE;
}

/* Fixnum operations */
static inline Value make_fixnum(int64_t n) {
    Value v;
    v.tag = TAG_FIXNUM;
    v.as.fixnum = n;
    return v;
}

static inline int64_t untag_fixnum(Value v) {
    return v.as.fixnum;
}

/* Float operations */
static inline bool is_float(Value v) {
    return v.tag == TAG_FLOAT;
}

static inline Value make_float(double d) {
    Value v;
    v.tag = TAG_FLOAT;
    v.as.floatnum = d;
    return v;
}

static inline double untag_float(Value v) {
    return v.as.floatnum;
}

/* Character operations */
static inline Value make_char(uint32_t c) {
    Value v;
    v.tag = TAG_CHAR;
    v.as.character = c;
    return v;
}

static inline uint32_t untag_char(Value v) {
    return v.as.character;
}

/* Pointer operations */
static inline Value tag_pointer(void* ptr) {
    Value v;
    v.tag = TAG_OBJECT;
    v.as.object = (struct Object*)ptr;
    return v;
}

static inline void* untag_pointer(Value v) {
    return (void*)v.as.object;
}

/* Object header (all heap objects start with this) */
struct Object {
    uint32_t type;      /* Object type (8 bits) + flags (24 bits) */
    uint32_t refcount;  /* Reference count */
    uint32_t size;      /* Type-dependent (length, capacity, hash, etc.) */
    void*    meta;      /* Metadata pointer (or NULL) */
#ifdef BEER_TRACK_ALLOCS
    struct Object* next_alloc;  /* Intrusive list of all live objects */
#endif
};

/* Object types */
typedef enum {
    TYPE_BIGINT = 0x01,
    TYPE_STRING = 0x12,
    TYPE_SYMBOL = 0x10,
    TYPE_KEYWORD = 0x11,
    TYPE_CONS = 0x20,
    TYPE_VECTOR = 0x21,
    TYPE_HASHMAP = 0x22,
    TYPE_FUNCTION = 0x30,
    TYPE_NATIVE_FN = 0x31,
    TYPE_VAR = 0x40,
    TYPE_NAMESPACE = 0x08,
    TYPE_STREAM = 0x50,
    TYPE_TASK = 0x60,
    TYPE_CHANNEL = 0x70,
    TYPE_HAMT_NODE = 0x80,
    TYPE_HAMT_COLLISION = 0x81,
    TYPE_ATOM = 0x85,
} ObjectType;

/* Get object type */
static inline uint8_t object_type(Value v) {
    if (!is_pointer(v)) return 0;
    struct Object* obj = (struct Object*)untag_pointer(v);
    return obj->type & 0xFF;
}

/* Utility functions (implemented in value.c) */
void value_print(Value v);
void value_println(Value v);
void value_print_readable(Value v);
size_t value_sprint_readable(Value v, char** buf, size_t* cap, size_t len);
bool value_equal(Value a, Value b);
const char* value_type_name(Value v);
bool value_valid(Value v);

#endif /* BEERLANG_VALUE_H */
