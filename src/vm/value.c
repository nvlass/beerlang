/* Value operations - Implementation
 *
 * Most value operations are inline in value.h, but here we provide
 * utilities and debug/print functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "beerlang.h"
#include "bigint.h"
#include "function.h"
#include "native.h"
#include "atom.h"

/* Print a value (for debugging) */
void value_print(Value v) {
    if (is_nil(v)) {
        printf("nil");
    } else if (is_true(v)) {
        printf("true");
    } else if (is_false(v)) {
        printf("false");
    } else if (is_fixnum(v)) {
        printf("%" PRId64, untag_fixnum(v));
    } else if (is_float(v)) {
        printf("%g", untag_float(v));
    } else if (is_char(v)) {
        uint32_t c = untag_char(v);
        if (c >= 32 && c < 127) {
            printf("\\%c", (char)c);
        } else {
            printf("\\u%04X", c);
        }
    } else if (is_pointer(v)) {
        uint8_t type = object_type(v);
        switch (type) {
            case TYPE_STRING:
                /* Print string contents without quotes */
                printf("%s", string_cstr(v));
                break;

            case TYPE_SYMBOL:
                printf("%s", symbol_name(v));
                break;

            case TYPE_KEYWORD:
                printf(":%s", keyword_name(v));
                break;

            case TYPE_BIGINT:
                bigint_print(v);
                break;

            case TYPE_CONS:
                list_print(v);
                break;

            case TYPE_VECTOR:
                vector_print(v);
                break;

            case TYPE_HASHMAP:
                hashmap_print(v);
                break;

            case TYPE_FUNCTION:
                printf("#<fn %s>", function_name(v));
                break;

            case TYPE_NATIVE_FN:
                printf("#<fn %s>", native_function_name(v));
                break;

            case TYPE_STREAM: {
                Stream* s = (Stream*)untag_pointer(v);
                switch (s->kind) {
                    case STREAM_STDIN:  printf("#<stream stdin>"); break;
                    case STREAM_STDOUT: printf("#<stream stdout>"); break;
                    case STREAM_STDERR: printf("#<stream stderr>"); break;
                    case STREAM_FILE:   printf("#<stream file:%d>", s->fd); break;
                }
                break;
            }

            case TYPE_TASK:
                printf("#<task>");
                break;
            case TYPE_CHANNEL:
                printf("#<channel>");
                break;
            case TYPE_ATOM: {
                Atom* a = (Atom*)untag_pointer(v);
                printf("#<atom ");
                value_print(a->value);
                printf(">");
                break;
            }

            default: {
                Object* obj = (Object*)untag_pointer(v);
                printf("#<object type=%d @%p>", obj->type & 0xFF, (void*)obj);
                break;
            }
        }
    } else {
        printf("#<unknown tag=%d>", get_tag(v));
    }
}

/* Print a value with newline */
void value_println(Value v) {
    value_print(v);
    printf("\n");
}

/* Print a value in readable mode (strings with quotes, chars with \) */
void value_print_readable(Value v) {
    if (is_nil(v)) {
        printf("nil");
    } else if (is_true(v)) {
        printf("true");
    } else if (is_false(v)) {
        printf("false");
    } else if (is_fixnum(v)) {
        printf("%" PRId64, untag_fixnum(v));
    } else if (is_float(v)) {
        printf("%g", untag_float(v));
    } else if (is_char(v)) {
        uint32_t c = untag_char(v);
        switch (c) {
            case '\n': printf("\\newline"); break;
            case '\t': printf("\\tab"); break;
            case '\r': printf("\\return"); break;
            case ' ':  printf("\\space"); break;
            default:
                if (c >= 32 && c < 127) {
                    printf("\\%c", (char)c);
                } else {
                    printf("\\u%04X", c);
                }
                break;
        }
    } else if (is_pointer(v)) {
        uint8_t type = object_type(v);
        switch (type) {
            case TYPE_STRING: {
                const char* s = string_cstr(v);
                printf("\"");
                while (*s) {
                    unsigned char ch = (unsigned char)*s;
                    switch (ch) {
                        case '"':  printf("\\\""); break;
                        case '\\': printf("\\\\"); break;
                        case '\n': printf("\\n"); break;
                        case '\t': printf("\\t"); break;
                        case '\r': printf("\\r"); break;
                        default:
                            if (ch < 32) {
                                printf("\\u%04X", ch);
                            } else {
                                putchar(ch);
                            }
                            break;
                    }
                    s++;
                }
                printf("\"");
                break;
            }
            case TYPE_SYMBOL:
                printf("%s", symbol_name(v));
                break;
            case TYPE_KEYWORD:
                printf(":%s", keyword_name(v));
                break;
            case TYPE_BIGINT:
                bigint_print(v);
                break;
            case TYPE_CONS:
                list_print_readable(v);
                break;
            case TYPE_VECTOR:
                vector_print_readable(v);
                break;
            case TYPE_HASHMAP:
                hashmap_print_readable(v);
                break;
            case TYPE_FUNCTION:
                printf("#<fn %s>", function_name(v));
                break;
            case TYPE_NATIVE_FN:
                printf("#<fn %s>", native_function_name(v));
                break;
            case TYPE_STREAM: {
                Stream* s = (Stream*)untag_pointer(v);
                switch (s->kind) {
                    case STREAM_STDIN:  printf("#<stream stdin>"); break;
                    case STREAM_STDOUT: printf("#<stream stdout>"); break;
                    case STREAM_STDERR: printf("#<stream stderr>"); break;
                    case STREAM_FILE:   printf("#<stream file:%d>", s->fd); break;
                }
                break;
            }
            case TYPE_TASK:
                printf("#<task>");
                break;
            case TYPE_CHANNEL:
                printf("#<channel>");
                break;
            case TYPE_ATOM: {
                Atom* a = (Atom*)untag_pointer(v);
                printf("#<atom ");
                value_print_readable(a->value);
                printf(">");
                break;
            }
            default: {
                Object* obj = (Object*)untag_pointer(v);
                printf("#<object type=%d @%p>", obj->type & 0xFF, (void*)obj);
                break;
            }
        }
    } else {
        printf("#<unknown tag=%d>", get_tag(v));
    }
}

/* Sprint readable: append readable representation to growable buffer */
static size_t sprint_str_readable(const char* s, char** buf, size_t* cap, size_t len) {
    /* Ensure space for opening quote */
    while (len + 2 > *cap) { *cap = (*cap < 64) ? 64 : *cap * 2; *buf = realloc(*buf, *cap); }
    (*buf)[len++] = '"';

    while (*s) {
        const char* esc = NULL;
        size_t esc_len = 0;
        char tmp[7];
        unsigned char ch = (unsigned char)*s;
        switch (ch) {
            case '"':  esc = "\\\""; esc_len = 2; break;
            case '\\': esc = "\\\\"; esc_len = 2; break;
            case '\n': esc = "\\n";  esc_len = 2; break;
            case '\t': esc = "\\t";  esc_len = 2; break;
            case '\r': esc = "\\r";  esc_len = 2; break;
            default:
                if (ch < 32) {
                    esc_len = (size_t)snprintf(tmp, sizeof(tmp), "\\u%04X", ch);
                    esc = tmp;
                } else {
                    tmp[0] = (char)ch; esc = tmp; esc_len = 1;
                }
                break;
        }
        while (len + esc_len + 2 > *cap) { *cap *= 2; *buf = realloc(*buf, *cap); }
        memcpy(*buf + len, esc, esc_len);
        len += esc_len;
        s++;
    }

    (*buf)[len++] = '"';
    return len;
}

size_t value_sprint_readable(Value v, char** buf, size_t* cap, size_t len) {
    char tmp[64];
    const char* s = NULL;
    size_t slen = 0;

    if (is_nil(v)) {
        s = "nil"; slen = 3;
    } else if (is_true(v)) {
        s = "true"; slen = 4;
    } else if (is_false(v)) {
        s = "false"; slen = 5;
    } else if (is_fixnum(v)) {
        slen = (size_t)snprintf(tmp, sizeof(tmp), "%" PRId64, untag_fixnum(v));
        s = tmp;
    } else if (is_float(v)) {
        slen = (size_t)snprintf(tmp, sizeof(tmp), "%g", untag_float(v));
        s = tmp;
    } else if (is_char(v)) {
        uint32_t c = untag_char(v);
        switch (c) {
            case '\n': s = "\\newline"; slen = 8; break;
            case '\t': s = "\\tab"; slen = 4; break;
            case '\r': s = "\\return"; slen = 7; break;
            case ' ':  s = "\\space"; slen = 6; break;
            default:
                if (c >= 32 && c < 127) {
                    slen = (size_t)snprintf(tmp, sizeof(tmp), "\\%c", (char)c);
                } else {
                    slen = (size_t)snprintf(tmp, sizeof(tmp), "\\u%04X", c);
                }
                s = tmp;
                break;
        }
    } else if (is_pointer(v)) {
        uint8_t type = object_type(v);
        if (type == TYPE_STRING) {
            return sprint_str_readable(string_cstr(v), buf, cap, len);
        }
        switch (type) {
            case TYPE_SYMBOL:
                s = symbol_name(v);
                slen = strlen(s);
                break;
            case TYPE_KEYWORD:
                slen = (size_t)snprintf(tmp, sizeof(tmp), ":%s", keyword_name(v));
                s = tmp;
                break;
            case TYPE_BIGINT:
                s = "#<bigint>"; slen = 9;
                break;
            case TYPE_CONS:
            case TYPE_VECTOR:
            case TYPE_HASHMAP:
            case TYPE_FUNCTION:
            case TYPE_NATIVE_FN: {
                /* Use open_memstream to capture value_print_readable output */
                char* mbuf = NULL;
                size_t mlen = 0;
                FILE* mf = open_memstream(&mbuf, &mlen);
                if (mf) {
                    FILE* old_stdout = stdout;
                    stdout = mf;
                    value_print_readable(v);
                    stdout = old_stdout;
                    fclose(mf);
                    while (len + mlen + 1 > *cap) {
                        *cap = (*cap < 64) ? 64 : *cap * 2;
                        *buf = realloc(*buf, *cap);
                    }
                    memcpy(*buf + len, mbuf, mlen);
                    free(mbuf);
                    return len + mlen;
                }
                s = value_type_name(v);
                slen = strlen(s);
                break;
            }
            default:
                s = value_type_name(v);
                slen = strlen(s);
                break;
        }
    } else {
        s = "#<unknown>"; slen = 10;
    }

    while (len + slen + 1 > *cap) {
        *cap = (*cap < 64) ? 64 : *cap * 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + len, s, slen);
    return len + slen;
}

/* Check if two values are equal */
/* Compare two sequential collections (list/vector) element-wise */
static bool seq_equal(Value a, Value b) {
    /* Get lengths */
    uint8_t type_a = object_type(a);
    uint8_t type_b = object_type(b);
    size_t len_a = (type_a == TYPE_VECTOR) ? vector_length(a) : (size_t)list_length(a);
    size_t len_b = (type_b == TYPE_VECTOR) ? vector_length(b) : (size_t)list_length(b);

    if (len_a != len_b) return false;

    /* Iterate both sequences */
    Value ca = a, cb = b;
    for (size_t i = 0; i < len_a; i++) {
        Value ea = (type_a == TYPE_VECTOR) ? vector_get(a, i) : car(ca);
        Value eb = (type_b == TYPE_VECTOR) ? vector_get(b, i) : car(cb);

        if (!value_equal(ea, eb)) return false;

        if (type_a == TYPE_CONS) ca = cdr(ca);
        if (type_b == TYPE_CONS) cb = cdr(cb);
    }
    return true;
}

bool value_equal(Value a, Value b) {
    /* Fast path: identical values */
    if (value_identical(a, b)) return true;

    /* Cross-type numeric equality: float/fixnum/bigint */
    {
        bool a_num = is_fixnum(a) || is_float(a) || (is_pointer(a) && object_type(a) == TYPE_BIGINT);
        bool b_num = is_fixnum(b) || is_float(b) || (is_pointer(b) && object_type(b) == TYPE_BIGINT);
        if (a_num && b_num) {
            /* Promote both to double for comparison */
            double da, db;
            if (is_float(a)) da = untag_float(a);
            else if (is_fixnum(a)) da = (double)untag_fixnum(a);
            else da = bigint_to_double(a);
            if (is_float(b)) db = untag_float(b);
            else if (is_fixnum(b)) db = (double)untag_fixnum(b);
            else db = bigint_to_double(b);
            return da == db;
        }
    }

    /* Different tags: check nil vs empty collection */
    if (get_tag(a) != get_tag(b)) {
        /* nil = empty vector, nil = empty list */
        if (is_nil(a) && is_pointer(b)) {
            uint8_t tb = object_type(b);
            if (tb == TYPE_VECTOR) return vector_length(b) == 0;
            if (tb == TYPE_CONS) return false; /* non-nil cons is never empty */
        }
        if (is_nil(b) && is_pointer(a)) {
            uint8_t ta = object_type(a);
            if (ta == TYPE_VECTOR) return vector_length(a) == 0;
            if (ta == TYPE_CONS) return false;
        }
        return false;
    }

    /* Both pointers - compare object contents */
    if (is_pointer(a) && is_pointer(b)) {
        uint8_t type_a = object_type(a);
        uint8_t type_b = object_type(b);

        /* Different types: check cross-type sequence equality */
        if (type_a != type_b) {
            if ((type_a == TYPE_CONS || type_a == TYPE_VECTOR) &&
                (type_b == TYPE_CONS || type_b == TYPE_VECTOR)) {
                return seq_equal(a, b);
            }
            return false;
        }

        /* Type-specific equality */
        switch (type_a) {
            case TYPE_STRING: {
                /* Compare string contents */
                return string_cmp(a, b) == 0;
            }

            case TYPE_SYMBOL:
            case TYPE_KEYWORD:
                /* Symbols and keywords are interned - pointer equality is correct */
                return a.as.object == b.as.object;

            case TYPE_CONS:
                /* Compare lists recursively */
                return list_equal(a, b);

            case TYPE_VECTOR:
                /* Compare vectors element-wise */
                return vector_equal(a, b);

            case TYPE_HASHMAP:
                /* Compare hashmaps key-value wise */
                return hashmap_equal(a, b);

            case TYPE_BIGINT:
                /* Compare bigint values */
                return bigint_cmp(a, b) == 0;

            default:
                /* For other types, use pointer equality */
                return a.as.object == b.as.object;
        }
    }

    return false;
}

/* Get type name as string (for debugging) */
const char* value_type_name(Value v) {
    if (is_nil(v)) return "nil";
    if (is_true(v)) return "true";
    if (is_false(v)) return "false";
    if (is_fixnum(v)) return "fixnum";
    if (is_float(v)) return "float";
    if (is_char(v)) return "char";
    if (is_pointer(v)) {
        uint8_t type = object_type(v);
        switch (type) {
            case TYPE_BIGINT: return "bigint";
            case TYPE_STRING: return "string";
            case TYPE_SYMBOL: return "symbol";
            case TYPE_KEYWORD: return "keyword";
            case TYPE_CONS: return "cons";
            case TYPE_VECTOR: return "vector";
            case TYPE_HASHMAP: return "hashmap";
            case TYPE_FUNCTION: return "function";
            case TYPE_NATIVE_FN: return "native-fn";
            case TYPE_VAR: return "var";
            case TYPE_NAMESPACE: return "namespace";
            case TYPE_STREAM: return "stream";
            case TYPE_TASK: return "task";
            case TYPE_CHANNEL: return "channel";
            case TYPE_ATOM: return "atom";
            default: return "unknown-object";
        }
    }
    return "unknown";
}

/* Validate a value (check invariants) */
bool value_valid(Value v) {
    switch (v.tag) {
        case TAG_NIL:
        case TAG_TRUE:
        case TAG_FALSE:
        case TAG_FIXNUM:
        case TAG_FLOAT:
        case TAG_CHAR:
            return true;
        case TAG_OBJECT:
            /* Pointer must be non-NULL */
            return v.as.object != NULL;
        default:
            return false;
    }
}
