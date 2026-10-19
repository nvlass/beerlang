/* StringBuilder implementation */

#include <stdlib.h>
#include <string.h>
#include "strbuilder.h"
#include "memory.h"

static void strbuilder_destroy(struct Object* obj) {
    free(((StringBuilder*)obj)->data);
}

void strbuilder_init(void) {
    object_register_destructor(TYPE_STRBUILDER, strbuilder_destroy);
}

Value strbuilder_new(size_t capacity) {
    if (capacity < 16) capacity = 16;
    StringBuilder* sb = (StringBuilder*)object_alloc(TYPE_STRBUILDER, sizeof(StringBuilder));
    sb->data = xmalloc(capacity);
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = capacity;
    return tag_pointer(sb);
}

void strbuilder_append(Value v, const char* s, size_t n) {
    StringBuilder* sb = (StringBuilder*)untag_pointer(v);
    if (sb->len + n + 1 > sb->cap) {
        size_t cap = sb->cap;
        while (sb->len + n + 1 > cap) cap *= 2;
        sb->data = xrealloc(sb->data, cap);
        sb->cap = cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}
