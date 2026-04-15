/* Bitwise integer operations and character utilities for beerlang
 *
 * Registers in beer.core:
 *   bit-and, bit-or, bit-xor, bit-not, bit-shift-left, bit-shift-right
 *   char-code, char
 */

#include <stdint.h>
#include <stdbool.h>
#include "native.h"
#include "vm.h"
#include "value.h"
#include "namespace.h"
#include "symbol.h"
#include "memory.h"
#include "core.h"

extern NamespaceRegistry* global_namespace_registry;

static void register_native_in_ns(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
}

/* Helper: extract int64 from fixnum or char value */
static bool coerce_int(Value v, int64_t* out) {
    if (is_fixnum(v)) { *out = untag_fixnum(v); return true; }
    if (is_char(v))   { *out = (int64_t)untag_char(v); return true; }
    return false;
}

/* (bit-and x y ...) — variadic bitwise AND; identity = -1 (all bits set) */
static Value native_bit_and(VM* vm, int argc, Value* argv) {
    int64_t result = (int64_t)-1;
    for (int i = 0; i < argc; i++) {
        int64_t n;
        if (!coerce_int(argv[i], &n)) {
            vm_error(vm, "bit-and: arguments must be integers");
            return VALUE_NIL;
        }
        result &= n;
    }
    return make_fixnum(result);
}

/* (bit-or x y ...) — variadic bitwise OR; identity = 0 */
static Value native_bit_or(VM* vm, int argc, Value* argv) {
    int64_t result = 0;
    for (int i = 0; i < argc; i++) {
        int64_t n;
        if (!coerce_int(argv[i], &n)) {
            vm_error(vm, "bit-or: arguments must be integers");
            return VALUE_NIL;
        }
        result |= n;
    }
    return make_fixnum(result);
}

/* (bit-xor x y ...) — variadic bitwise XOR; identity = 0 */
static Value native_bit_xor(VM* vm, int argc, Value* argv) {
    int64_t result = 0;
    for (int i = 0; i < argc; i++) {
        int64_t n;
        if (!coerce_int(argv[i], &n)) {
            vm_error(vm, "bit-xor: arguments must be integers");
            return VALUE_NIL;
        }
        result ^= n;
    }
    return make_fixnum(result);
}

/* (bit-not x) — bitwise complement (~x) */
static Value native_bit_not(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "bit-not: requires exactly 1 argument");
        return VALUE_NIL;
    }
    int64_t n;
    if (!coerce_int(argv[0], &n)) {
        vm_error(vm, "bit-not: argument must be an integer");
        return VALUE_NIL;
    }
    return make_fixnum(~n);
}

/* (bit-shift-left x n) — left shift x by n bits */
static Value native_bit_shift_left(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "bit-shift-left: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    int64_t n;
    if (!coerce_int(argv[0], &n)) {
        vm_error(vm, "bit-shift-left: first argument must be an integer");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[1])) {
        vm_error(vm, "bit-shift-left: shift amount must be an integer");
        return VALUE_NIL;
    }
    int64_t shift = untag_fixnum(argv[1]);
    if (shift < 0 || shift >= 64) {
        vm_error(vm, "bit-shift-left: shift amount must be in [0, 63]");
        return VALUE_NIL;
    }
    return make_fixnum((int64_t)((uint64_t)n << (unsigned int)shift));
}

/* (bit-shift-right x n) — arithmetic right shift (sign-extending) */
static Value native_bit_shift_right(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "bit-shift-right: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    int64_t n;
    if (!coerce_int(argv[0], &n)) {
        vm_error(vm, "bit-shift-right: first argument must be an integer");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[1])) {
        vm_error(vm, "bit-shift-right: shift amount must be an integer");
        return VALUE_NIL;
    }
    int64_t shift = untag_fixnum(argv[1]);
    if (shift < 0 || shift >= 64) {
        vm_error(vm, "bit-shift-right: shift amount must be in [0, 63]");
        return VALUE_NIL;
    }
    return make_fixnum(n >> (unsigned int)shift);
}

/* (char-code ch) — Unicode code point of character as fixnum */
static Value native_char_code(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "char-code: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_char(argv[0])) {
        vm_error(vm, "char-code: argument must be a character");
        return VALUE_NIL;
    }
    return make_fixnum((int64_t)untag_char(argv[0]));
}

/* (char n) — character with given Unicode code point */
static Value native_char_from_code(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "char: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[0])) {
        vm_error(vm, "char: argument must be an integer");
        return VALUE_NIL;
    }
    int64_t n = untag_fixnum(argv[0]);
    if (n < 0 || n > 0x10FFFF) {
        vm_error(vm, "char: code point out of range (0–0x10FFFF)");
        return VALUE_NIL;
    }
    return make_char((uint32_t)n);
}

void core_register_bits(void) {
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) return;

    register_native_in_ns(core_ns, "bit-and",         native_bit_and);
    register_native_in_ns(core_ns, "bit-or",          native_bit_or);
    register_native_in_ns(core_ns, "bit-xor",         native_bit_xor);
    register_native_in_ns(core_ns, "bit-not",         native_bit_not);
    register_native_in_ns(core_ns, "bit-shift-left",  native_bit_shift_left);
    register_native_in_ns(core_ns, "bit-shift-right", native_bit_shift_right);
    register_native_in_ns(core_ns, "char-code",       native_char_code);
    register_native_in_ns(core_ns, "char",            native_char_from_code);
}
