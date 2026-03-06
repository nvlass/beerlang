/* Symbol and Keyword - Interned identifiers
 *
 * Symbols and keywords are interned, meaning the same name always
 * returns the same object, allowing fast pointer-equality checks.
 *
 * Symbols: used for variable names, function names, etc.
 * Keywords: self-evaluating values starting with `:`, used as map keys
 *
 * Both can have optional namespace prefix: namespace/name
 */

#ifndef BEERLANG_SYMBOL_H
#define BEERLANG_SYMBOL_H

#include <stdbool.h>
#include <stddef.h>
#include "value.h"

/* Symbol and Keyword object structure (heap-allocated, interned)
 * Layout:
 *   Object header (16 bytes)
 *   uint32_t hash (cached hash value)
 *   uint16_t ns_len (namespace length in bytes, 0 if no namespace)
 *   uint16_t name_len (name length in bytes)
 *   char data[] (namespace/name, null-terminated)
 */

/* Initialize symbol/keyword subsystem (call once at startup) */
void symbol_init(void);

/* Shutdown symbol/keyword subsystem (call at exit) */
void symbol_shutdown(void);

/* Create or get interned symbol */
Value symbol_intern(const char* name);

/* Create or get interned symbol with namespace */
Value symbol_intern_ns(const char* ns, const char* name);

/* Create or get interned keyword */
Value keyword_intern(const char* name);

/* Create or get interned keyword with namespace */
Value keyword_intern_ns(const char* ns, const char* name);

/* Get symbol/keyword name (without namespace) */
const char* symbol_name(Value sym);
const char* keyword_name(Value kw);

/* Get full symbol/keyword string (namespace/name or just name) */
const char* symbol_str(Value sym);
const char* keyword_str(Value kw);

/* Check if symbol/keyword has namespace */
bool symbol_has_namespace(Value sym);
bool keyword_has_namespace(Value kw);

/* Equality - just pointer comparison since interned */
static inline bool symbol_eq(Value a, Value b) {
    return a.as.object == b.as.object;
}

static inline bool keyword_eq(Value a, Value b) {
    return a.as.object == b.as.object;
}

/* Get hash value */
uint32_t symbol_hash(Value sym);
uint32_t keyword_hash(Value kw);

/* Print symbol/keyword (for debugging) */
void symbol_print(Value sym);
void keyword_print(Value kw);

/* Get interning statistics */
typedef struct {
    size_t symbol_count;    /* Number of interned symbols */
    size_t keyword_count;   /* Number of interned keywords */
    size_t table_size;      /* Hash table size */
} InternStats;

InternStats symbol_stats(void);

#endif /* BEERLANG_SYMBOL_H */
