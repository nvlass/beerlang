/* Docs for natives: per-namespace tables, turned into meta maps on demand */

#include <string.h>
#include "docs.h"
#include "memory.h"
#include "hashmap.h"
#include "symbol.h"
#include "bstring.h"
#include "reader.h"

typedef struct {
    const char* ns;
    const NativeDoc* docs;
    size_t count;
} DocTable;

static DocTable* tables = NULL;
static size_t n_tables = 0;
static size_t cap_tables = 0;

void docs_register(const char* ns, const NativeDoc* docs, size_t count) {
    for (size_t i = 0; i < n_tables; i++) {
        if (tables[i].docs == docs) return;  /* runtime re-initialised */
    }
    if (n_tables == cap_tables) {
        cap_tables = cap_tables ? cap_tables * 2 : 16;
        tables = xrealloc(tables, cap_tables * sizeof(DocTable));
    }
    tables[n_tables++] = (DocTable){ ns, docs, count };
}

Value docs_lookup(const char* ns, const char* name) {
    for (size_t t = 0; t < n_tables; t++) {
        if (strcmp(tables[t].ns, ns) != 0) continue;
        for (size_t i = 0; i < tables[t].count; i++) {
            const NativeDoc* d = &tables[t].docs[i];
            if (strcmp(d->name, name) != 0) continue;
            Value m = hashmap_create_default();
            if (d->arglists) {
                Value arglists = read_string(d->arglists);
                hashmap_set(m, keyword_intern("arglists"), arglists);
                if (is_pointer(arglists)) object_release(arglists);
            }
            if (d->doc) {
                Value doc = string_from_cstr(d->doc);
                hashmap_set(m, keyword_intern("doc"), doc);
                object_release(doc);
            }
            return m;
        }
    }
    return VALUE_NIL;
}
