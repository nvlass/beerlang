/* beer.json namespace — native JSON parser and emitter
 *
 * Registers in beer.json:
 *   parse  (str) → value   objects → maps with string keys, arrays → vectors,
 *                          integers → fixnum/bigint, decimals → float
 *   emit   (x)   → str     maps/vectors/lists/sets, strings, keywords and
 *                          symbols (as strings), numbers, booleans, nil
 *
 * Errors are raised with vm_error, so they are catchable with try/catch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>
#include "native.h"
#include "vm.h"
#include "value.h"
#include "namespace.h"
#include "symbol.h"
#include "bstring.h"
#include "memory.h"
#include "core.h"
#include "vector.h"
#include "hashmap.h"
#include "set.h"
#include "cons.h"
#include "bigint.h"

extern NamespaceRegistry* global_namespace_registry;

#define JSON_MAX_DEPTH 512

static void register_native_in_ns(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
}

/* ================================================================
 * Growable byte buffer
 * ================================================================ */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} Buf;

static void buf_reserve(Buf* b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra) cap *= 2;
    b->data = xrealloc(b->data, cap);
    b->cap = cap;
}

static inline void buf_put(Buf* b, const char* s, size_t n) {
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static inline void buf_putc(Buf* b, char c) {
    buf_reserve(b, 1);
    b->data[b->len++] = c;
}

/* ================================================================
 * Parser
 * ================================================================ */

typedef struct {
    const char* start;
    const char* p;
    const char* end;
    int depth;
    char err[160];
    bool failed;
    Buf scratch;     /* reused for strings with escapes */
} Parser;

static Value parse_value(Parser* ps);

static Value parse_fail(Parser* ps, const char* what) {
    if (!ps->failed) {
        ps->failed = true;
        snprintf(ps->err, sizeof(ps->err), "json/parse: %s at offset %zu",
                 what, (size_t)(ps->p - ps->start));
    }
    return VALUE_NIL;
}

static inline void skip_ws(Parser* ps) {
    while (ps->p < ps->end &&
           (*ps->p == ' ' || *ps->p == '\n' || *ps->p == '\t' || *ps->p == '\r'))
        ps->p++;
}

static int hex4(const char* s) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static void put_utf8(Buf* b, uint32_t cp) {
    char out[4];
    size_t n;
    if (cp < 0x80)         { out[0] = (char)cp; n = 1; }
    else if (cp < 0x800)   { out[0] = (char)(0xC0 | (cp >> 6));
                             out[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12));
                             out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             out[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else                   { out[0] = (char)(0xF0 | (cp >> 18));
                             out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                             out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             out[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    buf_put(b, out, n);
}

/* ps->p points at the opening quote */
static Value parse_string(Parser* ps) {
    const char* s = ++ps->p;
    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p;
        if (c == '"') {
            Value v = string_from_buffer(s, (size_t)(ps->p - s));
            ps->p++;
            return v;
        }
        if (c == '\\') break;
        if (c < 0x20) return parse_fail(ps, "control character in string");
        ps->p++;
    }
    if (ps->p >= ps->end) return parse_fail(ps, "unterminated string");

    /* Slow path: escapes present */
    Buf* b = &ps->scratch;
    b->len = 0;
    buf_put(b, s, (size_t)(ps->p - s));
    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p;
        if (c == '"') {
            ps->p++;
            return string_from_buffer(b->data, b->len);
        }
        if (c < 0x20) return parse_fail(ps, "control character in string");
        if (c != '\\') { buf_putc(b, (char)c); ps->p++; continue; }

        if (ps->p + 1 >= ps->end) break;
        char e = ps->p[1];
        ps->p += 2;
        switch (e) {
            case '"':  buf_putc(b, '"');  break;
            case '\\': buf_putc(b, '\\'); break;
            case '/':  buf_putc(b, '/');  break;
            case 'b':  buf_putc(b, '\b'); break;
            case 'f':  buf_putc(b, '\f'); break;
            case 'n':  buf_putc(b, '\n'); break;
            case 'r':  buf_putc(b, '\r'); break;
            case 't':  buf_putc(b, '\t'); break;
            case 'u': {
                if (ps->end - ps->p < 4) return parse_fail(ps, "truncated \\u escape");
                int cp = hex4(ps->p);
                if (cp < 0) return parse_fail(ps, "bad \\u escape");
                ps->p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    int lo = -1;
                    if (ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u')
                        lo = hex4(ps->p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        ps->p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                put_utf8(b, (uint32_t)cp);
                break;
            }
            default:
                ps->p -= 1;
                return parse_fail(ps, "bad escape");
        }
    }
    return parse_fail(ps, "unterminated string");
}

static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

static Value parse_number(Parser* ps) {
    const char* s = ps->p;
    const char* p = s;
    bool is_int = true;

    if (p < ps->end && *p == '-') p++;
    if (p >= ps->end || !is_digit(*p)) return parse_fail(ps, "bad number");
    if (*p == '0') p++;
    else while (p < ps->end && is_digit(*p)) p++;
    if (p < ps->end && *p == '.') {
        is_int = false;
        p++;
        if (p >= ps->end || !is_digit(*p)) { ps->p = p; return parse_fail(ps, "bad number"); }
        while (p < ps->end && is_digit(*p)) p++;
    }
    if (p < ps->end && (*p == 'e' || *p == 'E')) {
        is_int = false;
        p++;
        if (p < ps->end && (*p == '+' || *p == '-')) p++;
        if (p >= ps->end || !is_digit(*p)) { ps->p = p; return parse_fail(ps, "bad number"); }
        while (p < ps->end && is_digit(*p)) p++;
    }

    size_t n = (size_t)(p - s);
    ps->p = p;

    if (!is_int) {
        char tmp[64];
        if (n < sizeof(tmp)) {
            memcpy(tmp, s, n);
            tmp[n] = '\0';
            return make_float(strtod(tmp, NULL));
        }
        char* big = xmalloc(n + 1);
        memcpy(big, s, n);
        big[n] = '\0';
        double d = strtod(big, NULL);
        free(big);
        return make_float(d);
    }

    size_t digits = n - (*s == '-' ? 1 : 0);
    if (digits <= 18) {
        int64_t v = 0;
        for (const char* q = (*s == '-') ? s + 1 : s; q < p; q++) v = v * 10 + (*q - '0');
        return make_fixnum(*s == '-' ? -v : v);
    }

    char* tmp = xmalloc(n + 1);
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    Value big = bigint_from_string(tmp, 10);
    free(tmp);
    if (is_nil(big)) return parse_fail(ps, "bad number");
    Value demoted = bigint_try_demote(big);
    if (is_fixnum(demoted)) {
        object_release(big);
        return demoted;
    }
    return big;
}

static Value parse_array(Parser* ps) {
    ps->p++;
    Value vec = vector_create(8);
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return vec; }
    for (;;) {
        Value v = parse_value(ps);
        if (ps->failed) { object_release(vec); return VALUE_NIL; }
        vector_push(vec, v);
        if (is_pointer(v)) object_release(v);
        skip_ws(ps);
        if (ps->p >= ps->end) { object_release(vec); return parse_fail(ps, "unterminated array"); }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; return vec; }
        object_release(vec);
        return parse_fail(ps, "expected , or ] in array");
    }
}

static Value parse_object(Parser* ps) {
    ps->p++;
    Value map = hashmap_create_default();
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return map; }
    for (;;) {
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != '"') {
            object_release(map);
            return parse_fail(ps, "expected string key in object");
        }
        Value key = parse_string(ps);
        if (ps->failed) { object_release(map); return VALUE_NIL; }
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            object_release(key);
            object_release(map);
            return parse_fail(ps, "expected : in object");
        }
        ps->p++;
        Value val = parse_value(ps);
        if (ps->failed) { object_release(key); object_release(map); return VALUE_NIL; }
        hashmap_set(map, key, val);
        object_release(key);
        if (is_pointer(val)) object_release(val);
        skip_ws(ps);
        if (ps->p >= ps->end) { object_release(map); return parse_fail(ps, "unterminated object"); }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; return map; }
        object_release(map);
        return parse_fail(ps, "expected , or } in object");
    }
}

static bool match_lit(Parser* ps, const char* lit, size_t n) {
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, lit, n) != 0) return false;
    ps->p += n;
    return true;
}

static Value parse_value(Parser* ps) {
    skip_ws(ps);
    if (ps->p >= ps->end) return parse_fail(ps, "unexpected end of input");
    if (++ps->depth > JSON_MAX_DEPTH) return parse_fail(ps, "nesting too deep");

    Value v;
    switch (*ps->p) {
        case '"': v = parse_string(ps); break;
        case '{': v = parse_object(ps); break;
        case '[': v = parse_array(ps);  break;
        case 't': v = match_lit(ps, "true", 4)  ? VALUE_TRUE  : parse_fail(ps, "unexpected token"); break;
        case 'f': v = match_lit(ps, "false", 5) ? VALUE_FALSE : parse_fail(ps, "unexpected token"); break;
        case 'n': v = match_lit(ps, "null", 4)  ? VALUE_NIL   : parse_fail(ps, "unexpected token"); break;
        default:
            if (*ps->p == '-' || is_digit(*ps->p)) v = parse_number(ps);
            else v = parse_fail(ps, "unexpected character");
    }
    ps->depth--;
    return v;
}

static Value native_json_parse(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "json/parse: requires a string");
        return VALUE_NIL;
    }
    const char* s = string_cstr(argv[0]);
    Parser ps = { .start = s, .p = s, .end = s + string_byte_length(argv[0]) };

    Value v = parse_value(&ps);
    if (!ps.failed) {
        skip_ws(&ps);
        if (ps.p < ps.end) {
            if (is_pointer(v)) object_release(v);
            parse_fail(&ps, "trailing characters");
        }
    }
    free(ps.scratch.data);
    if (ps.failed) {
        vm_error(vm, ps.err);
        return VALUE_NIL;
    }
    return v;
}

/* ================================================================
 * Emitter
 * ================================================================ */

typedef struct {
    Buf out;
    int depth;
    bool failed;
    char err[160];
} Emitter;

static void emit_value(Emitter* em, Value v);

static void emit_fail(Emitter* em, const char* msg, Value v) {
    if (em->failed) return;
    em->failed = true;
    snprintf(em->err, sizeof(em->err), "json/emit: %s%s", msg,
             v.tag == TAG_NIL ? "" : value_type_name(v));
}

static void emit_string_bytes(Emitter* em, const char* s, size_t n) {
    static const char hex[] = "0123456789abcdef";
    Buf* b = &em->out;
    buf_reserve(b, n + 2);
    b->data[b->len++] = '"';
    size_t run = 0;   /* start of the current run of bytes needing no escape */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\') continue;
        buf_put(b, s + run, i - run);
        run = i + 1;
        switch (c) {
            case '"':  buf_put(b, "\\\"", 2); break;
            case '\\': buf_put(b, "\\\\", 2); break;
            case '\n': buf_put(b, "\\n", 2);  break;
            case '\r': buf_put(b, "\\r", 2);  break;
            case '\t': buf_put(b, "\\t", 2);  break;
            case '\b': buf_put(b, "\\b", 2);  break;
            case '\f': buf_put(b, "\\f", 2);  break;
            default: {
                char u[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 0xF] };
                buf_put(b, u, 6);
            }
        }
    }
    buf_put(b, s + run, n - run);
    buf_putc(b, '"');
}

static void emit_cstr(Emitter* em, const char* s) {
    emit_string_bytes(em, s, strlen(s));
}

static void emit_float(Emitter* em, double d) {
    if (!isfinite(d)) {
        emit_fail(em, "cannot encode NaN or Infinity", VALUE_NIL);
        return;
    }
    char tmp[40];
    int n = 0;
    for (int prec = 15; prec <= 17; prec++) {
        n = snprintf(tmp, sizeof(tmp), "%.*g", prec, d);
        if (strtod(tmp, NULL) == d) break;
    }
    buf_put(&em->out, tmp, (size_t)n);
    /* Keep integral floats floats on the way back in: 10.0 → "10.0" */
    if (!strpbrk(tmp, ".eEn")) buf_put(&em->out, ".0", 2);
}

/* Map keys become strings; numbers are stringified like (str k) */
static void emit_key(Emitter* em, Value k) {
    if (is_string(k)) {
        emit_string_bytes(em, string_cstr(k), string_byte_length(k));
    } else if (is_pointer(k) && object_type(k) == TYPE_KEYWORD) {
        emit_cstr(em, keyword_str(k));
    } else if (is_pointer(k) && object_type(k) == TYPE_SYMBOL) {
        emit_cstr(em, symbol_str(k));
    } else if (is_fixnum(k)) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%" PRId64, untag_fixnum(k));
        emit_cstr(em, tmp);
    } else if (is_pointer(k) && object_type(k) == TYPE_BIGINT) {
        char* s = bigint_to_string(k, 10);
        emit_cstr(em, s);
        free(s);
    } else if (is_float(k)) {
        size_t mark = em->out.len;
        emit_float(em, untag_float(k));
        if (em->failed) return;
        size_t n = em->out.len - mark;
        char tmp[48];
        memcpy(tmp, em->out.data + mark, n);
        em->out.len = mark;
        emit_string_bytes(em, tmp, n);
    } else if (is_char(k)) {
        char tmp[5];
        size_t n = (size_t)utf8_encode(untag_char(k), tmp);
        emit_string_bytes(em, tmp, n);
    } else if (is_true(k) || is_false(k)) {
        emit_cstr(em, is_true(k) ? "true" : "false");
    } else {
        emit_fail(em, "cannot encode map key of type ", k);
    }
}

typedef struct {
    Emitter* em;
    bool first;
} MapCtx;

static void emit_entry(Value key, Value val, void* ctx) {
    MapCtx* mc = (MapCtx*)ctx;
    if (mc->em->failed) return;
    if (!mc->first) buf_putc(&mc->em->out, ',');
    mc->first = false;
    emit_key(mc->em, key);
    buf_putc(&mc->em->out, ':');
    emit_value(mc->em, val);
}

static void emit_value(Emitter* em, Value v) {
    if (em->failed) return;
    Buf* b = &em->out;

    switch (v.tag) {
        case TAG_NIL:   buf_put(b, "null", 4);  return;
        case TAG_TRUE:  buf_put(b, "true", 4);  return;
        case TAG_FALSE: buf_put(b, "false", 5); return;
        case TAG_FIXNUM: {
            char tmp[32];
            int n = snprintf(tmp, sizeof(tmp), "%" PRId64, untag_fixnum(v));
            buf_put(b, tmp, (size_t)n);
            return;
        }
        case TAG_FLOAT:
            emit_float(em, untag_float(v));
            return;
        case TAG_CHAR: {
            char tmp[5];
            size_t n = (size_t)utf8_encode(untag_char(v), tmp);
            emit_string_bytes(em, tmp, n);
            return;
        }
        case TAG_OBJECT:
            break;
        default:
            emit_fail(em, "cannot encode value of type ", v);
            return;
    }

    if (++em->depth > JSON_MAX_DEPTH) {
        emit_fail(em, "nesting too deep", VALUE_NIL);
        return;
    }

    switch (object_type(v)) {
        case TYPE_STRING:
            emit_string_bytes(em, string_cstr(v), string_byte_length(v));
            break;
        case TYPE_KEYWORD:
            emit_cstr(em, keyword_str(v));
            break;
        case TYPE_SYMBOL:
            emit_cstr(em, symbol_str(v));
            break;
        case TYPE_BIGINT: {
            char* s = bigint_to_string(v, 10);
            buf_put(b, s, strlen(s));
            free(s);
            break;
        }
        case TYPE_VECTOR: {
            size_t n = vector_length(v);
            buf_putc(b, '[');
            for (size_t i = 0; i < n && !em->failed; i++) {
                if (i) buf_putc(b, ',');
                emit_value(em, vector_get(v, i));
            }
            buf_putc(b, ']');
            break;
        }
        case TYPE_CONS: {
            buf_putc(b, '[');
            bool first = true;
            for (Value c = v; is_cons(c) && !em->failed; c = cdr(c)) {
                if (!first) buf_putc(b, ',');
                first = false;
                emit_value(em, car(c));
            }
            buf_putc(b, ']');
            break;
        }
        case TYPE_SET: {
            Value elems = set_elements(v);
            size_t n = vector_length(elems);
            buf_putc(b, '[');
            for (size_t i = 0; i < n && !em->failed; i++) {
                if (i) buf_putc(b, ',');
                emit_value(em, vector_get(elems, i));
            }
            buf_putc(b, ']');
            object_release(elems);
            break;
        }
        case TYPE_HASHMAP: {
            MapCtx mc = { em, true };
            buf_putc(b, '{');
            hashmap_foreach(v, emit_entry, &mc);
            buf_putc(b, '}');
            break;
        }
        default:
            emit_fail(em, "cannot encode value of type ", v);
    }
    em->depth--;
}

static Value native_json_emit(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "json/emit: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Emitter em = {0};
    emit_value(&em, argv[0]);
    if (em.failed) {
        free(em.out.data);
        vm_error(vm, em.err);
        return VALUE_NIL;
    }
    Value s = string_from_buffer(em.out.data ? em.out.data : "", em.out.len);
    free(em.out.data);
    return s;
}

void core_register_json(void) {
    Namespace* json_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.json");
    if (!json_ns) return;

    register_native_in_ns(json_ns, "parse", native_json_parse);
    register_native_in_ns(json_ns, "emit",  native_json_emit);
}
