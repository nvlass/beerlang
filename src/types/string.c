/* String implementation - Immutable UTF-8 strings */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include "bstring.h"
#include "beerlang.h"
#include "cons.h"
#include "vector.h"

/* String object layout */
typedef struct {
    struct Object header;
    uint32_t byte_len;    /* Byte length (not including null terminator) */
    uint32_t char_count;  /* Number of UTF-8 characters */
    uint32_t hash;        /* Cached hash value (0 if not computed) */
    char data[];          /* Null-terminated UTF-8 data (flexible array) */
} String;

/* UTF-8 utilities */

/* Get byte length of UTF-8 character from first byte */
int utf8_char_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;      /* 0xxxxxxx */
    if ((first_byte & 0xE0) == 0xC0) return 2;   /* 110xxxxx */
    if ((first_byte & 0xF0) == 0xE0) return 3;   /* 1110xxxx */
    if ((first_byte & 0xF8) == 0xF0) return 4;   /* 11110xxx */
    return 0;  /* Invalid */
}

/* Decode UTF-8 character and advance pointer */
uint32_t utf8_decode(const char** ptr) {
    const unsigned char* p = (const unsigned char*)*ptr;
    uint32_t codepoint;
    int len = utf8_char_length(*p);

    switch (len) {
        case 1:
            codepoint = *p;
            *ptr += 1;
            break;
        case 2:
            codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            *ptr += 2;
            break;
        case 3:
            codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            *ptr += 3;
            break;
        case 4:
            codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                        ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            *ptr += 4;
            break;
        default:
            codepoint = 0xFFFD;  /* Replacement character for invalid UTF-8 */
            *ptr += 1;
            break;
    }

    return codepoint;
}

/* Encode UTF-8 codepoint */
int utf8_encode(uint32_t codepoint, char* out) {
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint < 0x110000) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;  /* Invalid codepoint */
}

/* Count UTF-8 characters */
size_t utf8_count_chars(const char* data, size_t byte_len) {
    size_t count = 0;
    const char* end = data + byte_len;

    while (data < end) {
        int len = utf8_char_length((unsigned char)*data);
        if (len == 0) len = 1;  /* Skip invalid bytes */
        data += len;
        count++;
    }

    return count;
}

/* Validate UTF-8 encoding */
bool string_is_valid_utf8(const char* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    const unsigned char* end = p + len;

    while (p < end) {
        int char_len = utf8_char_length(*p);

        if (char_len == 0 || p + char_len > end) {
            return false;  /* Invalid length or truncated */
        }

        /* Check continuation bytes */
        for (int i = 1; i < char_len; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                return false;  /* Invalid continuation byte */
            }
        }

        p += char_len;
    }

    return true;
}

/* Destructor - no cleanup needed for inline data */
static void string_destructor(struct Object* obj) {
    (void)obj;  /* No additional cleanup needed */
}

/* Initialize string type (idempotent - safe to call multiple times) */
static void string_init_type(void) {
    object_register_destructor(TYPE_STRING, string_destructor);
}

/* Create string from buffer */
Value string_from_buffer(const char* buf, size_t byte_len) {
    string_init_type();

    /* Validate UTF-8 */
    if (!string_is_valid_utf8(buf, byte_len)) {
        return VALUE_NIL;  /* Invalid UTF-8 */
    }

    /* Calculate total size */
    size_t total_size = sizeof(String) + byte_len + 1;  /* +1 for null terminator */

    String* str = (String*)object_alloc(TYPE_STRING, total_size);
    Value obj = tag_pointer(str);

    /* Set string metadata */
    str->byte_len = (uint32_t)byte_len;
    str->char_count = (uint32_t)utf8_count_chars(buf, byte_len);
    str->hash = 0;  /* Not computed yet */

    /* Copy data and null-terminate */
    memcpy(str->data, buf, byte_len);
    str->data[byte_len] = '\0';

    return obj;
}

/* Create string from C string */
Value string_from_cstr(const char* cstr) {
    return string_from_buffer(cstr, strlen(cstr));
}

/* Create empty string */
Value string_empty(void) {
    return string_from_buffer("", 0);
}

/* Get byte length */
size_t string_byte_length(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    return s->byte_len;
}

/* Get character count */
size_t string_char_length(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    return s->char_count;
}

/* Get C string pointer */
const char* string_cstr(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    return s->data;
}

/* String concatenation */
Value string_concat(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_STRING);
    assert(is_pointer(b) && object_type(b) == TYPE_STRING);

    String* str_a = (String*)untag_pointer(a);
    String* str_b = (String*)untag_pointer(b);

    size_t len_a = str_a->byte_len;
    size_t len_b = str_b->byte_len;
    size_t total_len = len_a + len_b;

    /* Create buffer and concatenate */
    char* buf = mem_alloc(total_len);
    memcpy(buf, str_a->data, len_a);
    memcpy(buf + len_a, str_b->data, len_b);

    Value result = string_from_buffer(buf, total_len);
    mem_free(buf, total_len);

    return result;
}

/* String comparison */
int string_cmp(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_STRING);
    assert(is_pointer(b) && object_type(b) == TYPE_STRING);

    /* Fast path: same object */
    if (value_identical(a, b)) return 0;

    String* str_a = (String*)untag_pointer(a);
    String* str_b = (String*)untag_pointer(b);

    return strcmp(str_a->data, str_b->data);
}

/* Substring */
Value string_substring(Value str, size_t start, size_t end) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);

    String* s = (String*)untag_pointer(str);
    size_t byte_len = s->byte_len;

    /* Validate bounds */
    if (start > end || end > byte_len) {
        return VALUE_NIL;
    }

    return string_from_buffer(s->data + start, end - start);
}

/* Get character at index */
uint32_t string_char_at(Value str, size_t index) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);

    String* s = (String*)untag_pointer(str);

    if (index >= s->char_count) {
        return 0;  /* Out of bounds */
    }

    /* Walk through UTF-8 characters */
    const char* p = s->data;
    for (size_t i = 0; i < index; i++) {
        utf8_decode(&p);
    }

    return utf8_decode(&p);
}

/* Check if starts with prefix */
bool string_starts_with(Value str, Value prefix) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    assert(is_pointer(prefix) && object_type(prefix) == TYPE_STRING);

    String* s = (String*)untag_pointer(str);
    String* p = (String*)untag_pointer(prefix);

    size_t str_len = s->byte_len;
    size_t prefix_len = p->byte_len;

    if (prefix_len > str_len) {
        return false;
    }

    return memcmp(s->data, p->data, prefix_len) == 0;
}

/* Check if ends with suffix */
bool string_ends_with(Value str, Value suffix) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    assert(is_pointer(suffix) && object_type(suffix) == TYPE_STRING);

    String* s = (String*)untag_pointer(str);
    String* suf = (String*)untag_pointer(suffix);

    size_t str_len = s->byte_len;
    size_t suffix_len = suf->byte_len;

    if (suffix_len > str_len) {
        return false;
    }

    return memcmp(s->data + str_len - suffix_len, suf->data, suffix_len) == 0;
}

/* Check if contains substring */
bool string_contains(Value str, Value substr) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    assert(is_pointer(substr) && object_type(substr) == TYPE_STRING);

    String* s = (String*)untag_pointer(str);
    String* sub = (String*)untag_pointer(substr);

    return strstr(s->data, sub->data) != NULL;
}

/* Find index of substring */
int64_t string_index_of(Value str, Value substr) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    assert(is_pointer(substr) && object_type(substr) == TYPE_STRING);

    String* s = (String*)untag_pointer(str);
    String* sub = (String*)untag_pointer(substr);

    const char* found = strstr(s->data, sub->data);
    if (found == NULL) {
        return -1;
    }

    return found - s->data;
}

/* Check if empty */
bool string_is_empty(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    return s->byte_len == 0;
}

/* Compute hash (FNV-1a) */
uint32_t string_hash(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);

    /* Return cached hash if available */
    if (s->hash != 0) {
        return s->hash;
    }

    /* FNV-1a hash */
    uint32_t hash = 2166136261u;
    const unsigned char* data = (const unsigned char*)s->data;
    size_t len = s->byte_len;

    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }

    /* Cache the hash (cast away const - safe because we own the object) */
    s->hash = hash;

    return hash;
}

/* Print string */
void string_print(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    printf("\"%s\"", s->data);
}

/* Convert char index to byte offset */
int64_t string_char_to_byte_offset(Value str, size_t char_idx) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    if (char_idx > s->char_count) return -1;
    if (char_idx == 0) return 0;

    const char* p = s->data;
    for (size_t i = 0; i < char_idx; i++) {
        int len = utf8_char_length((unsigned char)*p);
        if (len == 0) len = 1;
        p += len;
    }
    return (int64_t)(p - s->data);
}

/* Substring by character indices */
Value string_subs(Value str, size_t start, size_t end) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    if (start > end || end > s->char_count) return VALUE_NIL;

    int64_t byte_start = string_char_to_byte_offset(str, start);
    int64_t byte_end = string_char_to_byte_offset(str, end);
    if (byte_start < 0 || byte_end < 0) return VALUE_NIL;

    return string_from_buffer(s->data + byte_start, (size_t)(byte_end - byte_start));
}

/* Upper case (ASCII only) */
Value string_upper(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    char* buf = malloc(s->byte_len + 1);
    for (size_t i = 0; i < s->byte_len; i++) {
        buf[i] = (char)toupper((unsigned char)s->data[i]);
    }
    buf[s->byte_len] = '\0';
    Value result = string_from_buffer(buf, s->byte_len);
    free(buf);
    return result;
}

/* Lower case (ASCII only) */
Value string_lower(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    char* buf = malloc(s->byte_len + 1);
    for (size_t i = 0; i < s->byte_len; i++) {
        buf[i] = (char)tolower((unsigned char)s->data[i]);
    }
    buf[s->byte_len] = '\0';
    Value result = string_from_buffer(buf, s->byte_len);
    free(buf);
    return result;
}

/* Trim whitespace */
Value string_trim(Value str) {
    assert(is_pointer(str) && object_type(str) == TYPE_STRING);
    String* s = (String*)untag_pointer(str);
    const char* start = s->data;
    const char* end = s->data + s->byte_len;

    while (start < end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)*(end - 1))) end--;

    return string_from_buffer(start, (size_t)(end - start));
}

/* Replace all occurrences */
Value string_replace(Value str, Value target, Value replacement) {
    String* s = (String*)untag_pointer(str);
    String* t = (String*)untag_pointer(target);
    String* r = (String*)untag_pointer(replacement);

    if (t->byte_len == 0) return str;

    /* Count occurrences */
    int count = 0;
    const char* p = s->data;
    while ((p = strstr(p, t->data)) != NULL) {
        count++;
        p += t->byte_len;
    }
    if (count == 0) {
        object_retain(str);
        return str;
    }

    size_t new_len = s->byte_len + (size_t)count * (r->byte_len - t->byte_len);
    char* buf = malloc(new_len + 1);
    char* out = buf;
    p = s->data;

    while (*p) {
        const char* found = strstr(p, t->data);
        if (!found) {
            size_t remaining = (size_t)(s->data + s->byte_len - p);
            memcpy(out, p, remaining);
            out += remaining;
            break;
        }
        size_t chunk = (size_t)(found - p);
        memcpy(out, p, chunk);
        out += chunk;
        memcpy(out, r->data, r->byte_len);
        out += r->byte_len;
        p = found + t->byte_len;
    }
    *out = '\0';

    Value result = string_from_buffer(buf, new_len);
    free(buf);
    return result;
}

/* Split on delimiter */
Value string_split(Value str, Value delim) {
    String* s = (String*)untag_pointer(str);
    String* d = (String*)untag_pointer(delim);

    if (d->byte_len == 0) {
        /* Split into individual characters */
        Value result = VALUE_NIL;
        const char* p = s->data + s->byte_len;
        /* Build list in reverse for O(n) */
        while (p > s->data) {
            /* Walk back one UTF-8 char */
            const char* prev = p - 1;
            while (prev > s->data && ((unsigned char)*prev & 0xC0) == 0x80) prev--;
            Value part = string_from_buffer(prev, (size_t)(p - prev));
            Value new_result = cons(part, result);
            object_release(part);
            if (is_pointer(result)) object_release(result);
            result = new_result;
            p = prev;
        }
        return result;
    }

    /* Split on delimiter */
    Value result = VALUE_NIL;
    const char* p = s->data;
    const char* end = s->data + s->byte_len;

    /* Collect parts in a temporary array, then build list */
    size_t parts_cap = 16;
    size_t parts_count = 0;
    Value* parts = malloc(sizeof(Value) * parts_cap);

    while (p <= end) {
        const char* found = (p < end) ? strstr(p, d->data) : NULL;
        const char* part_end = found ? found : end;
        Value part = string_from_buffer(p, (size_t)(part_end - p));

        if (parts_count >= parts_cap) {
            parts_cap *= 2;
            parts = realloc(parts, sizeof(Value) * parts_cap);
        }
        parts[parts_count++] = part;

        if (!found) break;
        p = found + d->byte_len;
    }

    /* Build list from array */
    result = list_from_array(parts, parts_count);
    for (size_t i = 0; i < parts_count; i++) {
        object_release(parts[i]);
    }
    free(parts);
    return result;
}

/* Join collection with separator */
Value string_join(Value sep, Value coll) {
    /* Use value_sprint (display mode) for each element */
    size_t cap = 64;
    char* buf = malloc(cap);
    size_t len = 0;

    const char* sep_str = NULL;
    size_t sep_len = 0;
    if (!is_nil(sep)) {
        String* s = (String*)untag_pointer(sep);
        sep_str = s->data;
        sep_len = s->byte_len;
    }

    bool first = true;
    if (is_cons(coll) || is_nil(coll)) {
        Value cur = coll;
        while (is_cons(cur)) {
            if (!first && sep_str) {
                while (len + sep_len + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, sep_str, sep_len);
                len += sep_len;
            }
            first = false;
            /* Use display-mode sprint for join (no quotes on strings) */
            Value elem = car(cur);
            char tmp[64];
            if (is_pointer(elem) && object_type(elem) == TYPE_STRING) {
                const char* es = string_cstr(elem);
                size_t elen = string_byte_length(elem);
                while (len + elen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, es, elen);
                len += elen;
            } else if (is_fixnum(elem)) {
                int n = snprintf(tmp, sizeof(tmp), "%" PRId64, untag_fixnum(elem));
                while (len + (size_t)n + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, tmp, (size_t)n);
                len += (size_t)n;
            } else if (is_nil(elem)) {
                /* skip or print "nil"? Clojure prints empty string for nil in join */
            } else {
                /* Generic: use value_sprint from core.c pattern */
                const char* s_name = value_type_name(elem);
                size_t slen = strlen(s_name);
                while (len + slen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, s_name, slen);
                len += slen;
            }
            cur = cdr(cur);
        }
    } else if (is_vector(coll)) {
        size_t vlen = vector_length(coll);
        for (size_t i = 0; i < vlen; i++) {
            if (!first && sep_str) {
                while (len + sep_len + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, sep_str, sep_len);
                len += sep_len;
            }
            first = false;
            Value elem = vector_get(coll, i);
            char tmp[64];
            if (is_pointer(elem) && object_type(elem) == TYPE_STRING) {
                const char* es = string_cstr(elem);
                size_t elen = string_byte_length(elem);
                while (len + elen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, es, elen);
                len += elen;
            } else if (is_fixnum(elem)) {
                int n = snprintf(tmp, sizeof(tmp), "%" PRId64, untag_fixnum(elem));
                while (len + (size_t)n + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, tmp, (size_t)n);
                len += (size_t)n;
            } else if (is_nil(elem)) {
                /* Clojure: nil -> empty string in join */
            } else if (is_char(elem)) {
                char cbuf[4];
                int clen = utf8_encode(untag_char(elem), cbuf);
                while (len + (size_t)clen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, cbuf, (size_t)clen);
                len += (size_t)clen;
            } else {
                /* Use the display-mode value_sprint helper pattern */
                const char* kw;
                size_t kwlen;
                if (is_pointer(elem) && object_type(elem) == TYPE_KEYWORD) {
                    char kwbuf[64];
                    kwlen = (size_t)snprintf(kwbuf, sizeof(kwbuf), ":%s", keyword_name(elem));
                    while (len + kwlen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                    memcpy(buf + len, kwbuf, kwlen);
                    len += kwlen;
                } else if (is_pointer(elem) && object_type(elem) == TYPE_SYMBOL) {
                    kw = symbol_name(elem);
                    kwlen = strlen(kw);
                    while (len + kwlen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                    memcpy(buf + len, kw, kwlen);
                    len += kwlen;
                } else if (is_true(elem)) {
                    kw = "true"; kwlen = 4;
                    while (len + kwlen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                    memcpy(buf + len, kw, kwlen);
                    len += kwlen;
                } else if (is_false(elem)) {
                    kw = "false"; kwlen = 5;
                    while (len + kwlen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                    memcpy(buf + len, kw, kwlen);
                    len += kwlen;
                }
            }
        }
    }

    buf[len] = '\0';
    Value result = string_from_buffer(buf, len);
    free(buf);
    return result;
}
