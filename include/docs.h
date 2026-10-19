/* Docs for natives: :doc / :arglists metadata for vars defined in C */

#ifndef BEERLANG_DOCS_H
#define BEERLANG_DOCS_H

#include <stddef.h>
#include "value.h"

typedef struct {
    const char* name;      /* var name in the namespace */
    const char* arglists;  /* readable list of param vectors, e.g. "([x] [x y])"; NULL if none */
    const char* doc;       /* NULL if none */
} NativeDoc;

/* Register docs for vars of namespace ns. Nothing is built here: `meta` on
 * a var whose metadata is nil looks the var up in its home namespace's
 * tables and stores the map it builds. ns and the table must stay valid for
 * the life of the runtime (string literals and a static array). */
void docs_register(const char* ns, const NativeDoc* docs, size_t count);

/* New meta map {:arglists .. :doc ..} for ns/name, or VALUE_NIL */
Value docs_lookup(const char* ns, const char* name);

/* Registers the beer.core table (called from core_init) */
void docs_register_core(void);

#endif /* BEERLANG_DOCS_H */
