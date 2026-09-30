/* ByteBuffer - mutable binary buffer type
 *
 * Contiguous, mutable, non-UTF8-validated byte storage with an NIO-ish
 * position/limit cursor. Meant for driver/DMA/wire-format work where
 * String's immutability and UTF-8 validation get in the way.
 *
 * See docs/beerlang-bytebuffer.md in the beeros repo for the full design.
 */

#ifndef BEERLANG_BYTEBUFFER_H
#define BEERLANG_BYTEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "value.h"

/* ByteBuffer object structure (heap-allocated)
 * Layout:
 *   Object header (16 bytes)
 *     - size field contains capacity in bytes
 *   uint32_t position
 *   uint32_t limit
 *   uint8_t data[] (NOT null-terminated, NOT UTF-8 validated, inline)
 */

/* Check if value is a bytebuffer */
static inline bool is_bytebuffer(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_BYTEBUFFER;
}

/* Create a zeroed buffer of the given capacity. position=0, limit=capacity. */
Value bytebuffer_alloc(size_t capacity);

/* Create a buffer that copies raw bytes (no UTF-8 validation). */
Value bytebuffer_from_buffer(const char* buf, size_t len);

size_t bytebuffer_capacity(Value b);
size_t bytebuffer_position(Value b);
void   bytebuffer_set_position(Value b, size_t pos);
size_t bytebuffer_limit(Value b);
void   bytebuffer_set_limit(Value b, size_t limit);

/* Raw pointer to data[0]. Caller must keep the buffer alive/retained. */
uint8_t* bytebuffer_data(Value b);

#endif /* BEERLANG_BYTEBUFFER_H */
