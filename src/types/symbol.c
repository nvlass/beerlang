/* Symbol and Keyword implementation with interning */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "symbol.h"
#include "beerlang.h"

/* Symbol/Keyword object layout */
typedef struct {
    struct Object header;
    uint32_t hash;       /* Cached hash value */
    uint16_t ns_len;     /* Namespace length (0 if no namespace) */
    uint16_t name_len;   /* Name length */
    char data[];         /* "namespace/name" or just "name", null-terminated */
} Symbol;

/* Interning table entry */
typedef struct InternEntry {
    Value value;              /* The interned symbol or keyword */
    struct InternEntry* next; /* Next entry in chain (for collisions) */
} InternEntry;

/* Interning tables (separate for symbols and keywords) */
#define INITIAL_TABLE_SIZE 256

static InternEntry** g_symbol_table = NULL;
static InternEntry** g_keyword_table = NULL;
static size_t g_symbol_table_size = 0;
static size_t g_keyword_table_size = 0;
static size_t g_symbol_count = 0;
static size_t g_keyword_count = 0;

/* Hash function (FNV-1a) */
static uint32_t hash_string(const char* str) {
    uint32_t hash = 2166136261u;
    const unsigned char* p = (const unsigned char*)str;

    while (*p) {
        hash ^= *p++;
        hash *= 16777619u;
    }

    return hash;
}

/* Destructor - no cleanup needed for inline data */
static void symbol_destructor(struct Object* obj) {
    (void)obj;  /* No additional cleanup needed */
}

static void keyword_destructor(struct Object* obj) {
    (void)obj;  /* No additional cleanup needed */
}

/* Initialize interning tables */
void symbol_init(void) {
    if (g_symbol_table == NULL) {
        g_symbol_table_size = INITIAL_TABLE_SIZE;
        g_symbol_table = calloc(g_symbol_table_size, sizeof(InternEntry*));

        g_keyword_table_size = INITIAL_TABLE_SIZE;
        g_keyword_table = calloc(g_keyword_table_size, sizeof(InternEntry*));

        object_register_destructor(TYPE_SYMBOL, symbol_destructor);
        object_register_destructor(TYPE_KEYWORD, keyword_destructor);
    }
}

/* Shutdown and free interning tables */
void symbol_shutdown(void) {
    if (g_symbol_table) {
        for (size_t i = 0; i < g_symbol_table_size; i++) {
            InternEntry* entry = g_symbol_table[i];
            while (entry) {
                InternEntry* next = entry->next;
                object_release(entry->value);  /* Release interned value */
                free(entry);
                entry = next;
            }
        }
        free(g_symbol_table);
        g_symbol_table = NULL;
    }

    if (g_keyword_table) {
        for (size_t i = 0; i < g_keyword_table_size; i++) {
            InternEntry* entry = g_keyword_table[i];
            while (entry) {
                InternEntry* next = entry->next;
                object_release(entry->value);  /* Release interned value */
                free(entry);
                entry = next;
            }
        }
        free(g_keyword_table);
        g_keyword_table = NULL;
    }
}

/* Create symbol/keyword object (internal) */
static Value create_symbol_object(uint8_t type, const char* ns, const char* name, uint32_t hash) {
    size_t ns_len = ns ? strlen(ns) : 0;
    size_t name_len = strlen(name);
    size_t total_len = ns_len + (ns_len > 0 ? 1 : 0) + name_len;  /* +1 for '/' if namespace exists */

    size_t total_size = sizeof(Symbol) + total_len + 1;  /* +1 for null terminator */

    Symbol* sym = (Symbol*)object_alloc(type, total_size);
    Value obj = tag_pointer(sym);

    sym->hash = hash;
    sym->ns_len = (uint16_t)ns_len;
    sym->name_len = (uint16_t)name_len;

    /* Build the full string */
    char* p = sym->data;
    if (ns_len > 0) {
        memcpy(p, ns, ns_len);
        p += ns_len;
        *p++ = '/';
    }
    memcpy(p, name, name_len);
    p[name_len] = '\0';

    return obj;
}

/* Intern a value in the given table */
static Value intern_value(uint8_t type, InternEntry*** table, size_t* table_size, size_t* count,
                         const char* ns, const char* name) {
    symbol_init();

    /* Build lookup key */
    char key[512];
    if (ns && *ns) {
        snprintf(key, sizeof(key), "%s/%s", ns, name);
    } else {
        snprintf(key, sizeof(key), "%s", name);
    }

    uint32_t hash = hash_string(key);
    size_t index = hash % (*table_size);

    /* Look for existing entry */
    InternEntry* entry = (*table)[index];
    while (entry) {
        Symbol* sym = (Symbol*)untag_pointer(entry->value);
        if (strcmp(sym->data, key) == 0) {
            return entry->value;  /* Already interned */
        }
        entry = entry->next;
    }

    /* Not found - create new symbol/keyword */
    Value value = create_symbol_object(type, ns, name, hash);

    /* Add to table */
    entry = malloc(sizeof(InternEntry));
    entry->value = value;
    entry->next = (*table)[index];
    (*table)[index] = entry;
    (*count)++;

    /* Don't retain - the table holds the only reference (refcount starts at 1) */

    return value;
}

/* Create or get interned symbol */
Value symbol_intern(const char* name) {
    return intern_value(TYPE_SYMBOL, &g_symbol_table, &g_symbol_table_size, &g_symbol_count,
                       NULL, name);
}

Value symbol_intern_ns(const char* ns, const char* name) {
    return intern_value(TYPE_SYMBOL, &g_symbol_table, &g_symbol_table_size, &g_symbol_count,
                       ns, name);
}

/* Create or get interned keyword */
Value keyword_intern(const char* name) {
    return intern_value(TYPE_KEYWORD, &g_keyword_table, &g_keyword_table_size, &g_keyword_count,
                       NULL, name);
}

Value keyword_intern_ns(const char* ns, const char* name) {
    return intern_value(TYPE_KEYWORD, &g_keyword_table, &g_keyword_table_size, &g_keyword_count,
                       ns, name);
}

/* Get name (without namespace) */
const char* symbol_name(Value sym) {
    assert(is_pointer(sym) && object_type(sym) == TYPE_SYMBOL);
    Symbol* s = (Symbol*)untag_pointer(sym);

    if (s->ns_len > 0) {
        return s->data + s->ns_len + 1;  /* Skip "namespace/" */
    }
    return s->data;
}

const char* keyword_name(Value kw) {
    assert(is_pointer(kw) && object_type(kw) == TYPE_KEYWORD);
    Symbol* s = (Symbol*)untag_pointer(kw);

    if (s->ns_len > 0) {
        return s->data + s->ns_len + 1;  /* Skip "namespace/" */
    }
    return s->data;
}

/* Get full string */
const char* symbol_str(Value sym) {
    assert(is_pointer(sym) && object_type(sym) == TYPE_SYMBOL);
    Symbol* s = (Symbol*)untag_pointer(sym);
    return s->data;
}

const char* keyword_str(Value kw) {
    assert(is_pointer(kw) && object_type(kw) == TYPE_KEYWORD);
    Symbol* s = (Symbol*)untag_pointer(kw);
    return s->data;
}

/* Check if has namespace */
bool symbol_has_namespace(Value sym) {
    assert(is_pointer(sym) && object_type(sym) == TYPE_SYMBOL);
    Symbol* s = (Symbol*)untag_pointer(sym);
    return s->ns_len > 0;
}

bool keyword_has_namespace(Value kw) {
    assert(is_pointer(kw) && object_type(kw) == TYPE_KEYWORD);
    Symbol* s = (Symbol*)untag_pointer(kw);
    return s->ns_len > 0;
}

/* Get hash value */
uint32_t symbol_hash(Value sym) {
    assert(is_pointer(sym) && object_type(sym) == TYPE_SYMBOL);
    Symbol* s = (Symbol*)untag_pointer(sym);
    return s->hash;
}

uint32_t keyword_hash(Value kw) {
    assert(is_pointer(kw) && object_type(kw) == TYPE_KEYWORD);
    Symbol* s = (Symbol*)untag_pointer(kw);
    return s->hash;
}

/* Print symbol/keyword */
void symbol_print(Value sym) {
    assert(is_pointer(sym) && object_type(sym) == TYPE_SYMBOL);
    Symbol* s = (Symbol*)untag_pointer(sym);
    printf("%s", s->data);
}

void keyword_print(Value kw) {
    assert(is_pointer(kw) && object_type(kw) == TYPE_KEYWORD);
    Symbol* s = (Symbol*)untag_pointer(kw);
    printf(":%s", s->data);
}

/* Get statistics */
InternStats symbol_stats(void) {
    InternStats stats;
    stats.symbol_count = g_symbol_count;
    stats.keyword_count = g_keyword_count;
    stats.table_size = g_symbol_table_size;  /* Same for both tables */
    return stats;
}
