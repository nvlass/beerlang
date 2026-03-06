/* String - Immutable UTF-8 strings
 *
 * Strings are heap-allocated, reference-counted, and immutable.
 * They store UTF-8 encoded text.
 */

#ifndef BEERLANG_STRING_H
#define BEERLANG_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "value.h"

/* String object structure (heap-allocated)
 * Layout:
 *   Object header (16 bytes)
 *     - size field contains byte length
 *   uint32_t char_count (number of UTF-8 characters)
 *   uint32_t hash (cached hash value, 0 if not computed)
 *   char data[] (null-terminated UTF-8 bytes, inline)
 */

/* Check if value is a string */
static inline bool is_string(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_STRING;
}

/* Create a string from a C string (copies the data) */
Value string_from_cstr(const char* cstr);

/* Create a string from a buffer with explicit length */
Value string_from_buffer(const char* buf, size_t byte_len);

/* Create an empty string */
Value string_empty(void);

/* Get byte length (number of bytes, not including null terminator) */
size_t string_byte_length(Value str);

/* Get character count (number of UTF-8 codepoints) */
size_t string_char_length(Value str);

/* Get C string pointer (read-only, null-terminated) */
const char* string_cstr(Value str);

/* String concatenation - returns new string */
Value string_concat(Value a, Value b);

/* String comparison - returns -1, 0, or 1 */
int string_cmp(Value a, Value b);

static inline bool string_eq(Value a, Value b) {
    return string_cmp(a, b) == 0;
}

static inline bool string_lt(Value a, Value b) {
    return string_cmp(a, b) < 0;
}

static inline bool string_le(Value a, Value b) {
    return string_cmp(a, b) <= 0;
}

static inline bool string_gt(Value a, Value b) {
    return string_cmp(a, b) > 0;
}

static inline bool string_ge(Value a, Value b) {
    return string_cmp(a, b) >= 0;
}

/* Substring - extract substring by byte offsets
 * Returns VALUE_NIL if offsets are invalid
 */
Value string_substring(Value str, size_t start, size_t end);

/* Get character at index (UTF-8 codepoint)
 * Returns the codepoint value, or 0 if index is out of bounds
 */
uint32_t string_char_at(Value str, size_t index);

/* Check if string starts with prefix */
bool string_starts_with(Value str, Value prefix);

/* Check if string ends with suffix */
bool string_ends_with(Value str, Value suffix);

/* Check if string contains substring */
bool string_contains(Value str, Value substr);

/* Find first occurrence of substring (returns byte offset, or -1 if not found) */
int64_t string_index_of(Value str, Value substr);

/* String predicates */
bool string_is_empty(Value str);

/* Get or compute hash value */
uint32_t string_hash(Value str);

/* Validate UTF-8 encoding */
bool string_is_valid_utf8(const char* data, size_t len);

/* Print string (for debugging) */
void string_print(Value str);

/* Convert char index to byte offset. Returns byte offset or -1 if out of bounds. */
int64_t string_char_to_byte_offset(Value str, size_t char_idx);

/* Substring by character indices (not byte offsets) */
Value string_subs(Value str, size_t start, size_t end);

/* Case conversion (ASCII only) */
Value string_upper(Value str);
Value string_lower(Value str);

/* Trim whitespace from both ends */
Value string_trim(Value str);

/* Replace all occurrences of target with replacement */
Value string_replace(Value str, Value target, Value replacement);

/* Split string on delimiter, returns cons list of strings */
Value string_split(Value str, Value delim);

/* Join a collection (cons list or vector) with separator */
Value string_join(Value sep, Value coll);

/* UTF-8 utilities */

/* Get the byte length of a UTF-8 character from its first byte */
int utf8_char_length(unsigned char first_byte);

/* Decode a UTF-8 character, returns codepoint and advances pointer */
uint32_t utf8_decode(const char** ptr);

/* Encode a UTF-8 codepoint, returns number of bytes written */
int utf8_encode(uint32_t codepoint, char* out);

/* Count UTF-8 characters in a buffer */
size_t utf8_count_chars(const char* data, size_t byte_len);

#endif /* BEERLANG_STRING_H */
