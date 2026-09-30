/* beer.bytes namespace — mutable ByteBuffer natives
 *
 * See docs/beerlang-bytebuffer.md (beeros repo) for the full design.
 * Registers in beer.bytes:
 *   alloc, from-string, capacity, position, position!, limit, limit!,
 *   remaining, rewind!, clear!, flip!
 *   u8/u8!, u16le/u16le!/u16be/u16be!, u32le/.../u32be!, u64le/.../u64be!
 *   i8/i8!, i16le/.../i16be!, i32le/.../i32be!, i64le/.../i64be!  (absolute)
 *   get-u8/put-u8!, get-u16le/put-u16le!/get-u16be/put-u16be!,
 *   get-u32le/.../get-u64be/put-u64be!                            (relative)
 *   fill!, copy!, blit-from-addr!, blit-to-addr!, slice
 *   ->string, ->string-lossy, hex, addr
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "native.h"
#include "vm.h"
#include "value.h"
#include "namespace.h"
#include "symbol.h"
#include "bstring.h"
#include "bytebuffer.h"
#include "memory.h"
#include "core.h"

extern NamespaceRegistry* global_namespace_registry;

static void register_native_in_ns(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
}

/* =================================================================
 * Endian-aware raw read/write helpers (byte-order explicit, no
 * dependence on host endianness)
 * ================================================================= */

static inline uint8_t  rd_u8(const uint8_t* p)  { return p[0]; }
static inline uint16_t rd_u16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint16_t rd_u16be(const uint8_t* p) { return ((uint16_t)p[0] << 8) | (uint16_t)p[1]; }
static inline uint32_t rd_u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint32_t rd_u32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline uint64_t rd_u64le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static inline uint64_t rd_u64be(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static inline void wr_u8(uint8_t* p, uint8_t v)  { p[0] = v; }
static inline void wr_u16le(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr_u16be(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr_u32le(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static inline void wr_u32be(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * (3 - i))); }
static inline void wr_u64le(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static inline void wr_u64be(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * (7 - i))); }

/* =================================================================
 * Construction / info / cursor
 * ================================================================= */

static Value native_bytes_alloc(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_fixnum(argv[0]) || untag_fixnum(argv[0]) < 0) {
        vm_error(vm, "bytes/alloc: requires 1 non-negative fixnum argument (capacity)");
        return VALUE_NIL;
    }
    return bytebuffer_alloc((size_t)untag_fixnum(argv[0]));
}

static Value native_bytes_from_string(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "bytes/from-string: requires 1 string argument");
        return VALUE_NIL;
    }
    return bytebuffer_from_buffer(string_cstr(argv[0]), string_byte_length(argv[0]));
}

static Value native_bytes_capacity(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/capacity: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    return make_fixnum((int64_t)bytebuffer_capacity(argv[0]));
}

static Value native_bytes_position(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/position: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    return make_fixnum((int64_t)bytebuffer_position(argv[0]));
}

/* NIO invariant: 0 <= position <= limit <= capacity */
static Value native_bytes_position_bang(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1])) {
        vm_error(vm, "bytes/position!: requires (buffer index)");
        return VALUE_NIL;
    }
    int64_t idx = untag_fixnum(argv[1]);
    if (idx < 0 || (size_t)idx > bytebuffer_limit(argv[0])) {
        vm_error(vm, "bytes/position!: position must be within [0, limit]");
        return VALUE_NIL;
    }
    bytebuffer_set_position(argv[0], (size_t)idx);
    return VALUE_NIL;
}

static Value native_bytes_limit(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/limit: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    return make_fixnum((int64_t)bytebuffer_limit(argv[0]));
}

static Value native_bytes_limit_bang(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1])) {
        vm_error(vm, "bytes/limit!: requires (buffer index)");
        return VALUE_NIL;
    }
    int64_t idx = untag_fixnum(argv[1]);
    if (idx < 0 || (size_t)idx > bytebuffer_capacity(argv[0])) {
        vm_error(vm, "bytes/limit!: limit must be within [0, capacity]");
        return VALUE_NIL;
    }
    bytebuffer_set_limit(argv[0], (size_t)idx);
    /* Maintain position <= limit invariant, like java.nio.Buffer.limit() */
    if (bytebuffer_position(argv[0]) > (size_t)idx) {
        bytebuffer_set_position(argv[0], (size_t)idx);
    }
    return VALUE_NIL;
}

static Value native_bytes_remaining(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/remaining: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    return make_fixnum((int64_t)(bytebuffer_limit(argv[0]) - bytebuffer_position(argv[0])));
}

static Value native_bytes_rewind_bang(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/rewind!: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    bytebuffer_set_position(argv[0], 0);
    return VALUE_NIL;
}

static Value native_bytes_clear_bang(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/clear!: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    bytebuffer_set_position(argv[0], 0);
    bytebuffer_set_limit(argv[0], bytebuffer_capacity(argv[0]));
    return VALUE_NIL;
}

static Value native_bytes_flip_bang(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/flip!: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    bytebuffer_set_limit(argv[0], bytebuffer_position(argv[0]));
    bytebuffer_set_position(argv[0], 0);
    return VALUE_NIL;
}

/* =================================================================
 * Absolute accessors (no cursor movement), bounds-checked against
 * capacity. Unsigned get/signed get differ in how the raw bits are
 * boxed; put is bit-pattern-identical for the signed/unsigned pair
 * at a given width, so one put native is registered under both names.
 * ================================================================= */

#define DEF_ABS_GET_U(fname, width, readfn) \
static Value native_bytes_##fname(VM* vm, int argc, Value* argv) { \
    if (argc != 2 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1])) { \
        vm_error(vm, "bytes/" #fname ": requires (buffer index)"); return VALUE_NIL; \
    } \
    int64_t idx = untag_fixnum(argv[1]); \
    if (idx < 0 || (size_t)idx + (width) > bytebuffer_capacity(argv[0])) { \
        vm_error(vm, "bytes/" #fname ": index out of bounds"); return VALUE_NIL; \
    } \
    return make_fixnum((int64_t)readfn(bytebuffer_data(argv[0]) + idx)); \
}

#define DEF_ABS_GET_S(fname, width, readfn, stype) \
static Value native_bytes_##fname(VM* vm, int argc, Value* argv) { \
    if (argc != 2 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1])) { \
        vm_error(vm, "bytes/" #fname ": requires (buffer index)"); return VALUE_NIL; \
    } \
    int64_t idx = untag_fixnum(argv[1]); \
    if (idx < 0 || (size_t)idx + (width) > bytebuffer_capacity(argv[0])) { \
        vm_error(vm, "bytes/" #fname ": index out of bounds"); return VALUE_NIL; \
    } \
    return make_fixnum((int64_t)(stype)readfn(bytebuffer_data(argv[0]) + idx)); \
}

#define DEF_ABS_PUT(fname, width, writefn, ctype, mask) \
static Value native_bytes_##fname(VM* vm, int argc, Value* argv) { \
    if (argc != 3 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1]) || !is_fixnum(argv[2])) { \
        vm_error(vm, "bytes/" #fname ": requires (buffer index value)"); return VALUE_NIL; \
    } \
    int64_t idx = untag_fixnum(argv[1]); \
    if (idx < 0 || (size_t)idx + (width) > bytebuffer_capacity(argv[0])) { \
        vm_error(vm, "bytes/" #fname ": index out of bounds"); return VALUE_NIL; \
    } \
    writefn(bytebuffer_data(argv[0]) + idx, (ctype)((uint64_t)untag_fixnum(argv[2]) & (mask))); \
    return VALUE_NIL; \
}

DEF_ABS_GET_U(u8,    1, rd_u8)
DEF_ABS_GET_U(u16le, 2, rd_u16le)
DEF_ABS_GET_U(u16be, 2, rd_u16be)
DEF_ABS_GET_U(u32le, 4, rd_u32le)
DEF_ABS_GET_U(u32be, 4, rd_u32be)
DEF_ABS_GET_U(u64le, 8, rd_u64le)
DEF_ABS_GET_U(u64be, 8, rd_u64be)

DEF_ABS_GET_S(i8,    1, rd_u8,    int8_t)
DEF_ABS_GET_S(i16le, 2, rd_u16le, int16_t)
DEF_ABS_GET_S(i16be, 2, rd_u16be, int16_t)
DEF_ABS_GET_S(i32le, 4, rd_u32le, int32_t)
DEF_ABS_GET_S(i32be, 4, rd_u32be, int32_t)
DEF_ABS_GET_S(i64le, 8, rd_u64le, int64_t)
DEF_ABS_GET_S(i64be, 8, rd_u64be, int64_t)

DEF_ABS_PUT(put8,    1, wr_u8,    uint8_t,  0xFFULL)
DEF_ABS_PUT(put16le, 2, wr_u16le, uint16_t, 0xFFFFULL)
DEF_ABS_PUT(put16be, 2, wr_u16be, uint16_t, 0xFFFFULL)
DEF_ABS_PUT(put32le, 4, wr_u32le, uint32_t, 0xFFFFFFFFULL)
DEF_ABS_PUT(put32be, 4, wr_u32be, uint32_t, 0xFFFFFFFFULL)
DEF_ABS_PUT(put64le, 8, wr_u64le, uint64_t, 0xFFFFFFFFFFFFFFFFULL)
DEF_ABS_PUT(put64be, 8, wr_u64be, uint64_t, 0xFFFFFFFFFFFFFFFFULL)

/* =================================================================
 * Relative accessors — advance position by the access width.
 * Bounds-checked against limit (not capacity), per NIO semantics.
 * ================================================================= */

#define DEF_REL_GET(fname, width, readfn) \
static Value native_bytes_rel_##fname(VM* vm, int argc, Value* argv) { \
    if (argc != 1 || !is_bytebuffer(argv[0])) { \
        vm_error(vm, "bytes/get-" #fname ": requires (buffer)"); return VALUE_NIL; \
    } \
    size_t pos = bytebuffer_position(argv[0]); \
    if (pos + (width) > bytebuffer_limit(argv[0])) { \
        vm_error(vm, "bytes/get-" #fname ": buffer underflow"); return VALUE_NIL; \
    } \
    Value r = make_fixnum((int64_t)readfn(bytebuffer_data(argv[0]) + pos)); \
    bytebuffer_set_position(argv[0], pos + (width)); \
    return r; \
}

#define DEF_REL_PUT(fname, width, writefn, ctype) \
static Value native_bytes_rel_put_##fname(VM* vm, int argc, Value* argv) { \
    if (argc != 2 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1])) { \
        vm_error(vm, "bytes/put-" #fname "!: requires (buffer value)"); return VALUE_NIL; \
    } \
    size_t pos = bytebuffer_position(argv[0]); \
    if (pos + (width) > bytebuffer_limit(argv[0])) { \
        vm_error(vm, "bytes/put-" #fname "!: buffer overflow"); return VALUE_NIL; \
    } \
    writefn(bytebuffer_data(argv[0]) + pos, (ctype)(uint64_t)untag_fixnum(argv[1])); \
    bytebuffer_set_position(argv[0], pos + (width)); \
    return VALUE_NIL; \
}

DEF_REL_GET(u8,    1, rd_u8)
DEF_REL_GET(u16le, 2, rd_u16le)
DEF_REL_GET(u16be, 2, rd_u16be)
DEF_REL_GET(u32le, 4, rd_u32le)
DEF_REL_GET(u32be, 4, rd_u32be)
DEF_REL_GET(u64le, 8, rd_u64le)
DEF_REL_GET(u64be, 8, rd_u64be)

DEF_REL_PUT(u8,    1, wr_u8,    uint8_t)
DEF_REL_PUT(u16le, 2, wr_u16le, uint16_t)
DEF_REL_PUT(u16be, 2, wr_u16be, uint16_t)
DEF_REL_PUT(u32le, 4, wr_u32le, uint32_t)
DEF_REL_PUT(u32be, 4, wr_u32be, uint32_t)
DEF_REL_PUT(u64le, 8, wr_u64le, uint64_t)
DEF_REL_PUT(u64be, 8, wr_u64be, uint64_t)

/* =================================================================
 * Bulk operations
 * ================================================================= */

static Value native_bytes_fill_bang(VM* vm, int argc, Value* argv) {
    if (argc != 4 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1]) ||
        !is_fixnum(argv[2]) || !is_fixnum(argv[3])) {
        vm_error(vm, "bytes/fill!: requires (buffer start len val)");
        return VALUE_NIL;
    }
    int64_t start = untag_fixnum(argv[1]);
    int64_t len = untag_fixnum(argv[2]);
    if (start < 0 || len < 0 || (size_t)start + (size_t)len > bytebuffer_capacity(argv[0])) {
        vm_error(vm, "bytes/fill!: range out of bounds");
        return VALUE_NIL;
    }
    memset(bytebuffer_data(argv[0]) + start, (int)(untag_fixnum(argv[3]) & 0xFF), (size_t)len);
    return VALUE_NIL;
}

static Value native_bytes_copy_bang(VM* vm, int argc, Value* argv) {
    if (argc != 5 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1]) ||
        !is_bytebuffer(argv[2]) || !is_fixnum(argv[3]) || !is_fixnum(argv[4])) {
        vm_error(vm, "bytes/copy!: requires (dst dst-off src src-off len)");
        return VALUE_NIL;
    }
    int64_t dst_off = untag_fixnum(argv[1]);
    int64_t src_off = untag_fixnum(argv[3]);
    int64_t len = untag_fixnum(argv[4]);
    if (dst_off < 0 || src_off < 0 || len < 0 ||
        (size_t)dst_off + (size_t)len > bytebuffer_capacity(argv[0]) ||
        (size_t)src_off + (size_t)len > bytebuffer_capacity(argv[2])) {
        vm_error(vm, "bytes/copy!: range out of bounds");
        return VALUE_NIL;
    }
    memmove(bytebuffer_data(argv[0]) + dst_off, bytebuffer_data(argv[2]) + src_off, (size_t)len);
    return VALUE_NIL;
}

/* (bytes/blit-from-addr! b off addr len) — raw pointer -> buffer.
 * Same trust model as CFFI: addr is whatever the caller says it is. */
static Value native_bytes_blit_from_addr_bang(VM* vm, int argc, Value* argv) {
    if (argc != 4 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1]) ||
        !is_fixnum(argv[2]) || !is_fixnum(argv[3])) {
        vm_error(vm, "bytes/blit-from-addr!: requires (buffer off addr len)");
        return VALUE_NIL;
    }
    int64_t off = untag_fixnum(argv[1]);
    int64_t len = untag_fixnum(argv[3]);
    if (off < 0 || len < 0 || (size_t)off + (size_t)len > bytebuffer_capacity(argv[0])) {
        vm_error(vm, "bytes/blit-from-addr!: range out of bounds");
        return VALUE_NIL;
    }
    const void* src = (const void*)(uintptr_t)untag_fixnum(argv[2]);
    memcpy(bytebuffer_data(argv[0]) + off, src, (size_t)len);
    return VALUE_NIL;
}

static Value native_bytes_blit_to_addr_bang(VM* vm, int argc, Value* argv) {
    if (argc != 4 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1]) ||
        !is_fixnum(argv[2]) || !is_fixnum(argv[3])) {
        vm_error(vm, "bytes/blit-to-addr!: requires (buffer off addr len)");
        return VALUE_NIL;
    }
    int64_t off = untag_fixnum(argv[1]);
    int64_t len = untag_fixnum(argv[3]);
    if (off < 0 || len < 0 || (size_t)off + (size_t)len > bytebuffer_capacity(argv[0])) {
        vm_error(vm, "bytes/blit-to-addr!: range out of bounds");
        return VALUE_NIL;
    }
    void* dst = (void*)(uintptr_t)untag_fixnum(argv[2]);
    memcpy(dst, bytebuffer_data(argv[0]) + off, (size_t)len);
    return VALUE_NIL;
}

static Value native_bytes_slice(VM* vm, int argc, Value* argv) {
    if (argc != 3 || !is_bytebuffer(argv[0]) || !is_fixnum(argv[1]) || !is_fixnum(argv[2])) {
        vm_error(vm, "bytes/slice: requires (buffer start len)");
        return VALUE_NIL;
    }
    int64_t start = untag_fixnum(argv[1]);
    int64_t len = untag_fixnum(argv[2]);
    if (start < 0 || len < 0 || (size_t)start + (size_t)len > bytebuffer_capacity(argv[0])) {
        vm_error(vm, "bytes/slice: range out of bounds");
        return VALUE_NIL;
    }
    /* Always a copy — no aliasing (see docs/beerlang-bytebuffer.md scope cuts) */
    return bytebuffer_from_buffer((const char*)(bytebuffer_data(argv[0]) + start), (size_t)len);
}

static Value native_bytes_to_string(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/->string: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    /* string_from_buffer validates UTF-8 and returns VALUE_NIL on failure */
    return string_from_buffer((const char*)bytebuffer_data(argv[0]), bytebuffer_capacity(argv[0]));
}

static Value native_bytes_to_string_lossy(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/->string-lossy: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    const uint8_t* data = bytebuffer_data(argv[0]);
    size_t n = bytebuffer_capacity(argv[0]);
    /* Worst case: every input byte becomes a 3-byte U+FFFD replacement */
    char* out = malloc(n * 3 + 1);
    size_t out_len = 0;
    size_t i = 0;
    while (i < n) {
        int char_len = utf8_char_length(data[i]);
        bool valid = char_len > 0 && i + (size_t)char_len <= n;
        if (valid) {
            for (int k = 1; k < char_len && valid; k++) {
                if ((data[i + (size_t)k] & 0xC0) != 0x80) valid = false;
            }
        }
        if (valid) {
            memcpy(out + out_len, data + i, (size_t)char_len);
            out_len += (size_t)char_len;
            i += (size_t)char_len;
        } else {
            out[out_len++] = (char)0xEF;
            out[out_len++] = (char)0xBF;
            out[out_len++] = (char)0xBD;
            i++;
        }
    }
    Value result = string_from_buffer(out, out_len);
    free(out);
    return result;
}

static Value native_bytes_hex(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/hex: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    const uint8_t* data = bytebuffer_data(argv[0]);
    size_t n = bytebuffer_capacity(argv[0]);
    char* out = malloc(n == 0 ? 1 : n * 3);
    size_t pos = 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0) out[pos++] = ' ';
        pos += (size_t)snprintf(out + pos, 3, "%02x", data[i]);
    }
    out[pos] = '\0';
    Value result = string_from_buffer(out, pos);
    free(out);
    return result;
}

static Value native_bytes_addr(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_bytebuffer(argv[0])) {
        vm_error(vm, "bytes/addr: requires 1 bytebuffer argument");
        return VALUE_NIL;
    }
    return make_fixnum((int64_t)(uintptr_t)bytebuffer_data(argv[0]));
}

/* =================================================================
 * Registration
 * ================================================================= */

void core_register_bytes(void) {
    Namespace* ns = namespace_registry_get_or_create(global_namespace_registry, "beer.bytes");
    if (!ns) return;

    register_native_in_ns(ns, "alloc",       native_bytes_alloc);
    register_native_in_ns(ns, "from-string", native_bytes_from_string);
    register_native_in_ns(ns, "capacity",    native_bytes_capacity);
    register_native_in_ns(ns, "position",    native_bytes_position);
    register_native_in_ns(ns, "position!",   native_bytes_position_bang);
    register_native_in_ns(ns, "limit",       native_bytes_limit);
    register_native_in_ns(ns, "limit!",      native_bytes_limit_bang);
    register_native_in_ns(ns, "remaining",   native_bytes_remaining);
    register_native_in_ns(ns, "rewind!",     native_bytes_rewind_bang);
    register_native_in_ns(ns, "clear!",      native_bytes_clear_bang);
    register_native_in_ns(ns, "flip!",       native_bytes_flip_bang);

    register_native_in_ns(ns, "u8",    native_bytes_u8);
    register_native_in_ns(ns, "u16le", native_bytes_u16le);
    register_native_in_ns(ns, "u16be", native_bytes_u16be);
    register_native_in_ns(ns, "u32le", native_bytes_u32le);
    register_native_in_ns(ns, "u32be", native_bytes_u32be);
    register_native_in_ns(ns, "u64le", native_bytes_u64le);
    register_native_in_ns(ns, "u64be", native_bytes_u64be);
    register_native_in_ns(ns, "i8",    native_bytes_i8);
    register_native_in_ns(ns, "i16le", native_bytes_i16le);
    register_native_in_ns(ns, "i16be", native_bytes_i16be);
    register_native_in_ns(ns, "i32le", native_bytes_i32le);
    register_native_in_ns(ns, "i32be", native_bytes_i32be);
    register_native_in_ns(ns, "i64le", native_bytes_i64le);
    register_native_in_ns(ns, "i64be", native_bytes_i64be);

    /* put natives are bit-pattern-identical for the u/i pair at a width */
    register_native_in_ns(ns, "u8!",    native_bytes_put8);
    register_native_in_ns(ns, "i8!",    native_bytes_put8);
    register_native_in_ns(ns, "u16le!", native_bytes_put16le);
    register_native_in_ns(ns, "i16le!", native_bytes_put16le);
    register_native_in_ns(ns, "u16be!", native_bytes_put16be);
    register_native_in_ns(ns, "i16be!", native_bytes_put16be);
    register_native_in_ns(ns, "u32le!", native_bytes_put32le);
    register_native_in_ns(ns, "i32le!", native_bytes_put32le);
    register_native_in_ns(ns, "u32be!", native_bytes_put32be);
    register_native_in_ns(ns, "i32be!", native_bytes_put32be);
    register_native_in_ns(ns, "u64le!", native_bytes_put64le);
    register_native_in_ns(ns, "i64le!", native_bytes_put64le);
    register_native_in_ns(ns, "u64be!", native_bytes_put64be);
    register_native_in_ns(ns, "i64be!", native_bytes_put64be);

    register_native_in_ns(ns, "get-u8",     native_bytes_rel_u8);
    register_native_in_ns(ns, "get-u16le",  native_bytes_rel_u16le);
    register_native_in_ns(ns, "get-u16be",  native_bytes_rel_u16be);
    register_native_in_ns(ns, "get-u32le",  native_bytes_rel_u32le);
    register_native_in_ns(ns, "get-u32be",  native_bytes_rel_u32be);
    register_native_in_ns(ns, "get-u64le",  native_bytes_rel_u64le);
    register_native_in_ns(ns, "get-u64be",  native_bytes_rel_u64be);
    register_native_in_ns(ns, "put-u8!",    native_bytes_rel_put_u8);
    register_native_in_ns(ns, "put-u16le!", native_bytes_rel_put_u16le);
    register_native_in_ns(ns, "put-u16be!", native_bytes_rel_put_u16be);
    register_native_in_ns(ns, "put-u32le!", native_bytes_rel_put_u32le);
    register_native_in_ns(ns, "put-u32be!", native_bytes_rel_put_u32be);
    register_native_in_ns(ns, "put-u64le!", native_bytes_rel_put_u64le);
    register_native_in_ns(ns, "put-u64be!", native_bytes_rel_put_u64be);

    register_native_in_ns(ns, "fill!",            native_bytes_fill_bang);
    register_native_in_ns(ns, "copy!",            native_bytes_copy_bang);
    register_native_in_ns(ns, "blit-from-addr!",  native_bytes_blit_from_addr_bang);
    register_native_in_ns(ns, "blit-to-addr!",    native_bytes_blit_to_addr_bang);
    register_native_in_ns(ns, "slice",            native_bytes_slice);
    register_native_in_ns(ns, "->string",         native_bytes_to_string);
    register_native_in_ns(ns, "->string-lossy",   native_bytes_to_string_lossy);
    register_native_in_ns(ns, "hex",              native_bytes_hex);
    register_native_in_ns(ns, "addr",             native_bytes_addr);
}
