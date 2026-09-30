/* ByteBuffer implementation - mutable binary buffer */

#include <stdlib.h>
#include <string.h>
#include "beerlang.h"
#include "bytebuffer.h"

/* ByteBuffer object layout
 *
 * header.size holds the true allocation size (header + data), set by
 * object_alloc and used by the free path for stats accounting — do not
 * repurpose it. capacity is the user-facing payload size. */
typedef struct {
    struct Object header;
    uint32_t capacity;
    uint32_t position;
    uint32_t limit;
    uint8_t  data[];   /* NOT null-terminated, NOT UTF-8 validated */
} ByteBuffer;

Value bytebuffer_alloc(size_t capacity) {
    size_t total_size = sizeof(ByteBuffer) + capacity;
    ByteBuffer* buf = (ByteBuffer*)object_alloc(TYPE_BYTEBUFFER, total_size);
    /* object_alloc uses calloc — data[] is already zeroed */
    buf->capacity = (uint32_t)capacity;
    buf->position = 0;
    buf->limit = (uint32_t)capacity;
    return tag_pointer(buf);
}

Value bytebuffer_from_buffer(const char* src, size_t len) {
    Value v = bytebuffer_alloc(len);
    ByteBuffer* buf = (ByteBuffer*)untag_pointer(v);
    memcpy(buf->data, src, len);
    return v;
}

size_t bytebuffer_capacity(Value b) {
    ByteBuffer* buf = (ByteBuffer*)untag_pointer(b);
    return buf->capacity;
}

size_t bytebuffer_position(Value b) {
    ByteBuffer* buf = (ByteBuffer*)untag_pointer(b);
    return buf->position;
}

void bytebuffer_set_position(Value b, size_t pos) {
    ByteBuffer* buf = (ByteBuffer*)untag_pointer(b);
    buf->position = (uint32_t)pos;
}

size_t bytebuffer_limit(Value b) {
    ByteBuffer* buf = (ByteBuffer*)untag_pointer(b);
    return buf->limit;
}

void bytebuffer_set_limit(Value b, size_t limit) {
    ByteBuffer* buf = (ByteBuffer*)untag_pointer(b);
    buf->limit = (uint32_t)limit;
}

uint8_t* bytebuffer_data(Value b) {
    ByteBuffer* buf = (ByteBuffer*)untag_pointer(b);
    return buf->data;
}
