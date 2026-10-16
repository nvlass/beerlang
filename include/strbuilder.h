/* StringBuilder - growable, mutable UTF-8 buffer for building strings
 *
 * (str/builder) → append into it with str/append! → (str b) for the result.
 * The buffer is a separate malloc so it can grow without moving the object.
 * Every append is valid UTF-8, so the contents always are.
 */

#ifndef BEERLANG_STRBUILDER_H
#define BEERLANG_STRBUILDER_H

#include <stddef.h>
#include <stdbool.h>
#include "value.h"

typedef struct StringBuilder {
    struct Object header;
    char*  data;   /* NUL-terminated when len < cap */
    size_t len;    /* bytes used */
    size_t cap;    /* bytes allocated */
} StringBuilder;

static inline bool is_strbuilder(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_STRBUILDER;
}

void strbuilder_init(void);
Value strbuilder_new(size_t capacity);
void strbuilder_append(Value sb, const char* s, size_t n);

#endif
