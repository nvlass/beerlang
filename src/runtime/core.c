/* Core Native Functions
 *
 * Essential native functions for beerlang runtime.
 * Currently includes arithmetic operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "native.h"
#include "vm.h"
#include "value.h"
#include "memory.h"
#include "fixnum.h"
#include "bigint.h"
#include "bstring.h"
#include "cons.h"
#include "vector.h"
#include "hashmap.h"
#include "function.h"
#include "namespace.h"
#include "symbol.h"
#include "reader.h"
#include "compiler.h"
#include "stream.h"
#include "task.h"
#include "channel.h"
#include "scheduler.h"
#include "io_reactor.h"
#include "tarindex.h"
#include "disasm.h"
#include <fcntl.h>

/* Forward declarations */
Value native_load(VM* vm, int argc, Value* argv);
static Value load_from_buffer(VM* caller_vm, const char* source, const char* name);
static Value native_macroexpand_1(VM* vm, int argc, Value* argv);
static Value native_macroexpand(VM* vm, int argc, Value* argv);
static size_t value_sprint(Value v, char** buf, size_t* cap, size_t len);

/* =================================================================
 * Arithmetic Operations
 * ================================================================= */

/* Helper: extract double from any numeric value */
static bool num_to_double(Value v, double* out) {
    if (is_float(v)) { *out = untag_float(v); return true; }
    if (is_fixnum(v)) { *out = (double)untag_fixnum(v); return true; }
    if (is_bigint(v)) { *out = bigint_to_double(v); return true; }
    return false;
}

/* Addition: (+) => 0, (+ x) => x, (+ x y ...) => sum */
static Value native_add(VM* vm, int argc, Value* argv) {
    if (argc == 0) return make_fixnum(0);
    if (argc == 1) { if (is_pointer(argv[0])) object_retain(argv[0]); return argv[0]; }

    Value result = argv[0];
    bool result_is_intermediate = false;
    bool result_is_float = is_float(result);

    for (int i = 1; i < argc; i++) {
        Value a = result;
        Value b = argv[i];

        /* Float promotion */
        if (result_is_float || is_float(b)) {
            double da, db;
            if (!num_to_double(a, &da) || !num_to_double(b, &db)) {
                if (result_is_intermediate) object_release(result);
                vm_error(vm, "+: arguments must be numbers");
                return VALUE_NIL;
            }
            if (result_is_intermediate) object_release(a);
            result = make_float(da + db);
            result_is_intermediate = false;
            result_is_float = true;
            continue;
        }

        if (is_fixnum(a) && is_fixnum(b)) {
            Value sum;
            if (!fixnum_add_checked(a, b, &sum)) {
                if (result_is_intermediate) object_release(a);
                result = sum;
                result_is_intermediate = false;
                continue;
            }
            Value a_big = bigint_from_fixnum(a);
            Value b_big = bigint_from_fixnum(b);
            Value big_result = bigint_add(a_big, b_big);
            object_release(a_big);
            object_release(b_big);
            if (result_is_intermediate) object_release(a);
            result = big_result;
            result_is_intermediate = true;
            continue;
        }

        if ((is_fixnum(a) || is_bigint(a)) && (is_fixnum(b) || is_bigint(b))) {
            Value a_big = is_fixnum(a) ? bigint_from_fixnum(a) : a;
            Value b_big = is_fixnum(b) ? bigint_from_fixnum(b) : b;
            Value big_result = bigint_add(a_big, b_big);
            if (is_fixnum(a)) object_release(a_big);
            if (is_fixnum(b)) object_release(b_big);
            if (result_is_intermediate) object_release(a);
            result = big_result;
            result_is_intermediate = true;
            continue;
        }

        if (result_is_intermediate) object_release(result);
        vm_error(vm, "+: arguments must be numbers");
        return VALUE_NIL;
    }

    return result;
}

/* Subtraction: (- x) => -x, (- x y ...) => x - y - ... */
static Value native_sub(VM* vm, int argc, Value* argv) {
    if (argc == 0) {
        vm_error(vm, "-: requires at least 1 argument");
        return VALUE_NIL;
    }

    /* (- x) => negation */
    if (argc == 1) {
        Value a = argv[0];
        if (is_float(a)) return make_float(-untag_float(a));
        if (is_fixnum(a)) {
            Value result;
            if (!fixnum_neg_checked(a, &result)) return result;
            Value a_big = bigint_from_fixnum(a);
            Value neg = bigint_neg(a_big);
            object_release(a_big);
            return neg;
        }
        if (is_bigint(a)) return bigint_neg(a);
        vm_error(vm, "-: argument must be a number");
        return VALUE_NIL;
    }

    Value result = argv[0];
    bool result_is_intermediate = false;
    bool result_is_float = is_float(result);

    for (int i = 1; i < argc; i++) {
        Value a = result;
        Value b = argv[i];

        if (result_is_float || is_float(b)) {
            double da, db;
            if (!num_to_double(a, &da) || !num_to_double(b, &db)) {
                if (result_is_intermediate) object_release(result);
                vm_error(vm, "-: arguments must be numbers");
                return VALUE_NIL;
            }
            if (result_is_intermediate) object_release(a);
            result = make_float(da - db);
            result_is_intermediate = false;
            result_is_float = true;
            continue;
        }

        if (is_fixnum(a) && is_fixnum(b)) {
            Value diff;
            if (!fixnum_sub_checked(a, b, &diff)) {
                if (result_is_intermediate) object_release(a);
                result = diff;
                result_is_intermediate = false;
                continue;
            }
            Value a_big = bigint_from_fixnum(a);
            Value b_big = bigint_from_fixnum(b);
            Value big_result = bigint_sub(a_big, b_big);
            object_release(a_big);
            object_release(b_big);
            if (result_is_intermediate) object_release(a);
            result = big_result;
            result_is_intermediate = true;
            continue;
        }

        if ((is_fixnum(a) || is_bigint(a)) && (is_fixnum(b) || is_bigint(b))) {
            Value a_big = is_fixnum(a) ? bigint_from_fixnum(a) : a;
            Value b_big = is_fixnum(b) ? bigint_from_fixnum(b) : b;
            Value big_result = bigint_sub(a_big, b_big);
            if (is_fixnum(a)) object_release(a_big);
            if (is_fixnum(b)) object_release(b_big);
            if (result_is_intermediate) object_release(a);
            result = big_result;
            result_is_intermediate = true;
            continue;
        }

        if (result_is_intermediate) object_release(result);
        vm_error(vm, "-: arguments must be numbers");
        return VALUE_NIL;
    }

    return result;
}

/* Multiplication: (*) => 1, (* x) => x, (* x y ...) => product */
static Value native_mul(VM* vm, int argc, Value* argv) {
    if (argc == 0) return make_fixnum(1);
    if (argc == 1) { if (is_pointer(argv[0])) object_retain(argv[0]); return argv[0]; }

    Value result = argv[0];
    bool result_is_intermediate = false;
    bool result_is_float = is_float(result);

    for (int i = 1; i < argc; i++) {
        Value a = result;
        Value b = argv[i];

        if (result_is_float || is_float(b)) {
            double da, db;
            if (!num_to_double(a, &da) || !num_to_double(b, &db)) {
                if (result_is_intermediate) object_release(result);
                vm_error(vm, "*: arguments must be numbers");
                return VALUE_NIL;
            }
            if (result_is_intermediate) object_release(a);
            result = make_float(da * db);
            result_is_intermediate = false;
            result_is_float = true;
            continue;
        }

        if (is_fixnum(a) && is_fixnum(b)) {
            Value prod;
            if (!fixnum_mul_checked(a, b, &prod)) {
                if (result_is_intermediate) object_release(a);
                result = prod;
                result_is_intermediate = false;
                continue;
            }
            Value a_big = bigint_from_fixnum(a);
            Value b_big = bigint_from_fixnum(b);
            Value big_result = bigint_mul(a_big, b_big);
            object_release(a_big);
            object_release(b_big);
            if (result_is_intermediate) object_release(a);
            result = big_result;
            result_is_intermediate = true;
            continue;
        }

        if ((is_fixnum(a) || is_bigint(a)) && (is_fixnum(b) || is_bigint(b))) {
            Value a_big = is_fixnum(a) ? bigint_from_fixnum(a) : a;
            Value b_big = is_fixnum(b) ? bigint_from_fixnum(b) : b;
            Value big_result = bigint_mul(a_big, b_big);
            if (is_fixnum(a)) object_release(a_big);
            if (is_fixnum(b)) object_release(b_big);
            if (result_is_intermediate) object_release(a);
            result = big_result;
            result_is_intermediate = true;
            continue;
        }

        if (result_is_intermediate) object_release(result);
        vm_error(vm, "*: arguments must be numbers");
        return VALUE_NIL;
    }

    return result;
}

/* Division: (/ & args) - variadic. Returns float for non-exact integer division. */
static Value native_div(VM* vm, int argc, Value* argv) {
    if (argc == 0) {
        vm_error(vm, "/: requires at least 1 argument");
        return VALUE_NIL;
    }

    /* (/ x) => 1/x (reciprocal) */
    if (argc == 1) {
        Value a = argv[0];
        double da;
        if (!num_to_double(a, &da)) {
            vm_error(vm, "/: argument must be a number");
            return VALUE_NIL;
        }
        if (da == 0.0) {
            vm_error(vm, "/: division by zero");
            return VALUE_NIL;
        }
        return make_float(1.0 / da);
    }

    /* (/ x y ...) => x / y / ... */
    Value result = argv[0];
    bool result_is_float = is_float(result);
    bool result_is_intermediate = false;

    for (int i = 1; i < argc; i++) {
        Value a = result;
        Value b = argv[i];

        /* Float path */
        if (result_is_float || is_float(b)) {
            double da, db;
            if (!num_to_double(a, &da) || !num_to_double(b, &db)) {
                if (result_is_intermediate) object_release(result);
                vm_error(vm, "/: arguments must be numbers");
                return VALUE_NIL;
            }
            if (db == 0.0) {
                if (result_is_intermediate) object_release(a);
                vm_error(vm, "/: division by zero");
                return VALUE_NIL;
            }
            if (result_is_intermediate) object_release(a);
            result = make_float(da / db);
            result_is_intermediate = false;
            result_is_float = true;
            continue;
        }

        /* Check for division by zero */
        if ((is_fixnum(b) && untag_fixnum(b) == 0) ||
            (is_bigint(b) && bigint_is_zero(b))) {
            if (result_is_intermediate) object_release(a);
            vm_error(vm, "/: division by zero");
            return VALUE_NIL;
        }

        /* Both fixnums */
        if (is_fixnum(a) && is_fixnum(b)) {
            int64_t av = untag_fixnum(a), bv = untag_fixnum(b);
            if (result_is_intermediate) object_release(a);
            if (av % bv != 0) {
                result = make_float((double)av / (double)bv);
                result_is_float = true;
            } else {
                result = make_fixnum(av / bv);
            }
            result_is_intermediate = false;
            continue;
        }

        /* At least one bigint */
        if (is_bigint(a) || is_bigint(b)) {
            Value a_big = is_fixnum(a) ? bigint_from_fixnum(a) : a;
            Value b_big = is_fixnum(b) ? bigint_from_fixnum(b) : b;
            Value big_result = bigint_div(a_big, b_big);
            if (is_fixnum(a)) object_release(a_big);
            if (is_fixnum(b)) object_release(b_big);
            if (result_is_intermediate) object_release(a);
            result = big_result;
            result_is_intermediate = true;
            continue;
        }

        if (result_is_intermediate) object_release(result);
        vm_error(vm, "/: arguments must be numbers");
        return VALUE_NIL;
    }

    return result;
}

/* mod: (mod a b) - modulus (result has sign of divisor, like Clojure) */
static Value native_mod(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "mod: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[0]) || !is_fixnum(argv[1])) {
        vm_error(vm, "mod: arguments must be integers");
        return VALUE_NIL;
    }
    int64_t a = untag_fixnum(argv[0]);
    int64_t b = untag_fixnum(argv[1]);
    if (b == 0) {
        vm_error(vm, "mod: division by zero");
        return VALUE_NIL;
    }
    /* Clojure-style mod: result has sign of divisor */
    int64_t r = a % b;
    if (r != 0 && ((r ^ b) < 0)) r += b;
    return make_fixnum(r);
}

/* rem: (rem a b) - remainder (result has sign of dividend, like C %) */
static Value native_rem(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "rem: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[0]) || !is_fixnum(argv[1])) {
        vm_error(vm, "rem: arguments must be integers");
        return VALUE_NIL;
    }
    int64_t a = untag_fixnum(argv[0]);
    int64_t b = untag_fixnum(argv[1]);
    if (b == 0) {
        vm_error(vm, "rem: division by zero");
        return VALUE_NIL;
    }
    return make_fixnum(a % b);
}

/* quot: (quot a b) - integer truncating division */
static Value native_quot(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "quot: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    Value a = argv[0], b = argv[1];

    /* Both fixnums */
    if (is_fixnum(a) && is_fixnum(b)) {
        int64_t bv = untag_fixnum(b);
        if (bv == 0) { vm_error(vm, "quot: division by zero"); return VALUE_NIL; }
        return make_fixnum(untag_fixnum(a) / bv);
    }

    /* Float → truncate to int */
    if (is_float(a) || is_float(b)) {
        double da, db;
        if (!num_to_double(a, &da) || !num_to_double(b, &db)) {
            vm_error(vm, "quot: arguments must be numbers");
            return VALUE_NIL;
        }
        if (db == 0.0) { vm_error(vm, "quot: division by zero"); return VALUE_NIL; }
        double q = da / db;
        return make_fixnum((int64_t)(q < 0 ? -(-q) : q));  /* trunc toward zero */
    }

    /* Bigint */
    if ((is_fixnum(a) || is_bigint(a)) && (is_fixnum(b) || is_bigint(b))) {
        Value a_big = is_fixnum(a) ? bigint_from_fixnum(a) : a;
        Value b_big = is_fixnum(b) ? bigint_from_fixnum(b) : b;
        if (bigint_is_zero(b_big)) {
            if (is_fixnum(a)) object_release(a_big);
            if (is_fixnum(b)) object_release(b_big);
            vm_error(vm, "quot: division by zero");
            return VALUE_NIL;
        }
        Value result = bigint_div(a_big, b_big);
        if (is_fixnum(a)) object_release(a_big);
        if (is_fixnum(b)) object_release(b_big);
        return result;
    }

    vm_error(vm, "quot: arguments must be numbers");
    return VALUE_NIL;
}

/* float?: (float? x) => true if x is a float */
static Value native_float_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "float?: requires exactly 1 argument"); return VALUE_NIL; }
    return is_float(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

/* int?: (int? x) => true if x is a fixnum or bigint */
static Value native_int_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "int?: requires exactly 1 argument"); return VALUE_NIL; }
    return (is_fixnum(argv[0]) || is_bigint(argv[0])) ? VALUE_TRUE : VALUE_FALSE;
}

/* float: (float x) => coerce to float */
static Value native_float(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "float: requires exactly 1 argument"); return VALUE_NIL; }
    double d;
    if (!num_to_double(argv[0], &d)) {
        vm_error(vm, "float: argument must be a number");
        return VALUE_NIL;
    }
    return make_float(d);
}

/* int: (int x) => coerce to fixnum (truncate toward zero) */
static Value native_int(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "int: requires exactly 1 argument"); return VALUE_NIL; }
    Value v = argv[0];
    if (is_fixnum(v)) return v;
    if (is_float(v)) return make_fixnum((int64_t)untag_float(v));
    if (is_bigint(v)) {
        int64_t n;
        if (bigint_to_int64(v, &n)) return make_fixnum(n);
        vm_error(vm, "int: bigint too large for fixnum");
        return VALUE_NIL;
    }
    vm_error(vm, "int: argument must be a number");
    return VALUE_NIL;
}

/* =================================================================
 * I/O Functions
 * ================================================================= */

/* Helper: write a value's display representation to a stream */
static void stream_write_value(Value out, Value v, bool readable) {
    char* buf = NULL;
    size_t cap = 0;
    size_t len = 0;

    if (readable) {
        cap = 64;
        buf = malloc(cap);
        len = value_sprint_readable(v, &buf, &cap, 0);
    } else {
        cap = 64;
        buf = malloc(cap);
        len = value_sprint(v, &buf, &cap, 0);
    }

    stream_write_string(out, buf, len);
    free(buf);
}

/* Print: (print arg1 arg2 ...) - prints values separated by spaces */
static Value native_print(VM* vm, int argc, Value* argv) {
    (void)vm;
    Value out = stream_get_stdout();
    for (int i = 0; i < argc; i++) {
        if (i > 0) stream_write_string(out, " ", 1);
        stream_write_value(out, argv[i], false);
    }
    stream_flush(out);
    return VALUE_NIL;
}

/* Println: (println arg1 arg2 ...) - prints values with newline */
static Value native_println(VM* vm, int argc, Value* argv) {
    (void)vm;
    Value out = stream_get_stdout();
    for (int i = 0; i < argc; i++) {
        if (i > 0) stream_write_string(out, " ", 1);
        stream_write_value(out, argv[i], false);
    }
    stream_write_string(out, "\n", 1);
    stream_flush(out);
    return VALUE_NIL;
}

/* =================================================================
 * Comparison Functions
 * ================================================================= */

/* Helper: compare two numeric values. Returns -2 on type error. */
static int compare_numbers(Value a, Value b) {
    /* Float promotion */
    if (is_float(a) || is_float(b)) {
        double da, db;
        if (!num_to_double(a, &da) || !num_to_double(b, &db)) return -2;
        return (da < db) ? -1 : (da > db) ? 1 : 0;
    }
    if (is_fixnum(a) && is_fixnum(b)) {
        int64_t av = untag_fixnum(a), bv = untag_fixnum(b);
        return (av < bv) ? -1 : (av > bv) ? 1 : 0;
    }
    if ((is_fixnum(a) || is_bigint(a)) && (is_fixnum(b) || is_bigint(b))) {
        Value a_big = is_fixnum(a) ? bigint_from_fixnum(a) : a;
        Value b_big = is_fixnum(b) ? bigint_from_fixnum(b) : b;
        int cmp = bigint_cmp(a_big, b_big);
        if (is_fixnum(a)) object_release(a_big);
        if (is_fixnum(b)) object_release(b_big);
        return cmp;
    }
    return -2; /* type error */
}

/* Equality: (= a b ...) - true if all values are equal */
static Value native_eq(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, "=: requires at least 2 arguments");
        return VALUE_NIL;
    }

    for (int i = 0; i < argc - 1; i++) {
        if (!value_equal(argv[i], argv[i + 1])) return VALUE_FALSE;
    }
    return VALUE_TRUE;
}

/* Less than: (< a b ...) - true if strictly increasing */
static Value native_lt(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, "<: requires at least 2 arguments");
        return VALUE_NIL;
    }

    for (int i = 0; i < argc - 1; i++) {
        int cmp = compare_numbers(argv[i], argv[i + 1]);
        if (cmp == -2) {
            vm_error(vm, "<: arguments must be numbers");
            return VALUE_NIL;
        }
        if (cmp >= 0) return VALUE_FALSE;
    }
    return VALUE_TRUE;
}

/* Greater than: (> a b ...) - true if strictly decreasing */
static Value native_gt(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, ">: requires at least 2 arguments");
        return VALUE_NIL;
    }

    for (int i = 0; i < argc - 1; i++) {
        int cmp = compare_numbers(argv[i], argv[i + 1]);
        if (cmp == -2) {
            vm_error(vm, ">: arguments must be numbers");
            return VALUE_NIL;
        }
        if (cmp <= 0) return VALUE_FALSE;
    }
    return VALUE_TRUE;
}

/* Less than or equal: (<= a b ...) - true if non-decreasing */
static Value native_lte(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, "<=: requires at least 2 arguments");
        return VALUE_NIL;
    }

    for (int i = 0; i < argc - 1; i++) {
        int cmp = compare_numbers(argv[i], argv[i + 1]);
        if (cmp == -2) {
            vm_error(vm, "<=: arguments must be numbers");
            return VALUE_NIL;
        }
        if (cmp > 0) return VALUE_FALSE;
    }
    return VALUE_TRUE;
}

/* Greater than or equal: (>= a b ...) - true if non-increasing */
static Value native_gte(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, ">=: requires at least 2 arguments");
        return VALUE_NIL;
    }

    for (int i = 0; i < argc - 1; i++) {
        int cmp = compare_numbers(argv[i], argv[i + 1]);
        if (cmp == -2) {
            vm_error(vm, ">=: arguments must be numbers");
            return VALUE_NIL;
        }
        if (cmp < 0) return VALUE_FALSE;
    }
    return VALUE_TRUE;
}

/* =================================================================
 * Collection Functions
 * ================================================================= */

/* list: (list & args) => create a list from args */
static Value native_list(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc == 0) return VALUE_NIL;
    return list_from_array(argv, argc);
}

/* vector: (vector & args) => create a vector from args */
static Value native_vector(VM* vm, int argc, Value* argv) {
    (void)vm;
    return vector_from_array(argv, argc);
}

/* hash-map: (hash-map k1 v1 k2 v2 ...) => create a hashmap */
static Value native_hash_map(VM* vm, int argc, Value* argv) {
    if (argc % 2 != 0) {
        vm_error(vm, "hash-map: requires even number of arguments");
        return VALUE_NIL;
    }
    Value vec = vector_from_array(argv, argc);
    Value map = hashmap_from_vec(vec);
    object_release(vec);
    return map;
}

/* cons: (cons x coll) => prepend x to coll */
static Value native_cons(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "cons: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    Value x = argv[0];
    Value coll = argv[1];

    /* If coll is a vector, convert to list first */
    if (is_vector(coll)) {
        Value lst = vector_to_list(coll);
        Value result = cons(x, lst);
        object_release(lst);
        return result;
    }

    /* coll must be a list (cons or nil) */
    if (!is_nil(coll) && !is_cons(coll)) {
        vm_error(vm, "cons: second argument must be a sequence");
        return VALUE_NIL;
    }
    return cons(x, coll);
}

/* first: (first coll) => first element of list or vector or string */
static Value native_first(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "first: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    if (is_nil(coll)) return VALUE_NIL;
    if (is_cons(coll)) { Value v = car(coll); if (is_pointer(v)) object_retain(v); return v; }
    if (is_vector(coll)) {
        if (vector_length(coll) == 0) return VALUE_NIL;
        Value v = vector_first(coll); if (is_pointer(v)) object_retain(v); return v;
    }
    if (is_string(coll)) {
        if (string_char_length(coll) == 0) return VALUE_NIL;
        return make_char(string_char_at(coll, 0));
    }
    vm_error(vm, "first: argument must be a sequence");
    return VALUE_NIL;
}

/* rest: (rest coll) => all but first element */
static Value native_rest(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "rest: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    if (is_nil(coll)) return VALUE_NIL;
    if (is_cons(coll)) { Value v = cdr(coll); if (is_pointer(v)) object_retain(v); return v; }
    if (is_vector(coll)) {
        size_t len = vector_length(coll);
        if (len <= 1) return VALUE_NIL;
        Value result = vector_slice(coll, 1, len);
        Value lst = vector_to_list(result);
        object_release(result);
        return lst;
    }
    if (is_string(coll)) {
        size_t len = string_char_length(coll);
        if (len <= 1) return VALUE_NIL;
        /* Build cons list of remaining chars */
        Value result = VALUE_NIL;
        for (size_t i = len - 1; i >= 1; i--) {
            Value ch = make_char(string_char_at(coll, i));
            Value new_result = cons(ch, result);
            if (is_pointer(result)) object_release(result);
            result = new_result;
        }
        return result;
    }
    vm_error(vm, "rest: argument must be a sequence");
    return VALUE_NIL;
}

/* nth: (nth coll n) => nth element */
static Value native_nth(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "nth: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    Value idx_val = argv[1];
    if (!is_fixnum(idx_val)) {
        vm_error(vm, "nth: index must be an integer");
        return VALUE_NIL;
    }
    int64_t idx = untag_fixnum(idx_val);
    if (idx < 0) {
        vm_error(vm, "nth: index out of bounds");
        return VALUE_NIL;
    }

    if (is_cons(coll)) { Value v = list_nth(coll, (size_t)idx); if (is_pointer(v)) object_retain(v); return v; }
    if (is_vector(coll)) {
        if ((size_t)idx >= vector_length(coll)) {
            vm_error(vm, "nth: index out of bounds");
            return VALUE_NIL;
        }
        Value v = vector_get(coll, (size_t)idx); if (is_pointer(v)) object_retain(v); return v;
    }
    if (is_string(coll)) {
        if ((size_t)idx >= string_char_length(coll)) {
            vm_error(vm, "nth: index out of bounds");
            return VALUE_NIL;
        }
        return make_char(string_char_at(coll, (size_t)idx));
    }
    if (is_nil(coll)) {
        vm_error(vm, "nth: index out of bounds");
        return VALUE_NIL;
    }
    vm_error(vm, "nth: argument must be a sequence");
    return VALUE_NIL;
}

/* count: (count coll) => number of elements */
static Value native_count(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "count: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    if (is_nil(coll)) return make_fixnum(0);
    if (is_cons(coll)) return make_fixnum(list_length(coll));
    if (is_vector(coll)) return make_fixnum((int64_t)vector_length(coll));
    if (is_hashmap(coll)) return make_fixnum((int64_t)hashmap_size(coll));
    if (is_string(coll)) return make_fixnum((int64_t)string_char_length(coll));
    vm_error(vm, "count: argument must be a collection or nil");
    return VALUE_NIL;
}

/* conj: (conj coll item) => add item to collection */
static Value native_conj(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, "conj: requires at least 2 arguments");
        return VALUE_NIL;
    }
    Value coll = argv[0];

    if (is_nil(coll)) {
        /* nil treated as empty list — conj prepends */
        Value result = VALUE_NIL;
        for (int i = argc - 1; i >= 1; i--) {
            Value new_result = cons(argv[i], result);
            if (i < argc - 1) object_release(result);
            result = new_result;
        }
        return result;
    }

    if (is_cons(coll)) {
        /* Lists: conj prepends */
        Value result = coll;
        object_retain(result);
        for (int i = 1; i < argc; i++) {
            Value new_result = cons(argv[i], result);
            object_release(result);
            result = new_result;
        }
        return result;
    }

    if (is_vector(coll)) {
        /* Vectors: conj appends */
        Value result = vector_clone(coll);
        for (int i = 1; i < argc; i++) {
            vector_push(result, argv[i]);
        }
        return result;
    }

    if (is_hashmap(coll)) {
        /* Maps: conj expects [k v] vectors */
        Value result = coll;
        object_retain(result);
        for (int i = 1; i < argc; i++) {
            if (!is_vector(argv[i]) || vector_length(argv[i]) != 2) {
                object_release(result);
                vm_error(vm, "conj: map entries must be [key value] vectors");
                return VALUE_NIL;
            }
            Value next = hashmap_assoc(result, vector_get(argv[i], 0), vector_get(argv[i], 1));
            object_release(result);
            result = next;
        }
        return result;
    }

    vm_error(vm, "conj: first argument must be a collection or nil");
    return VALUE_NIL;
}

/* empty?: (empty? coll) => true if coll has no elements or is nil */
static Value native_empty_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "empty?: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    if (is_nil(coll)) return VALUE_TRUE;
    if (is_cons(coll)) return VALUE_FALSE; /* a cons is never empty */
    if (is_vector(coll)) return vector_length(coll) == 0 ? VALUE_TRUE : VALUE_FALSE;
    if (is_hashmap(coll)) return hashmap_size(coll) == 0 ? VALUE_TRUE : VALUE_FALSE;
    if (is_string(coll)) return string_char_length(coll) == 0 ? VALUE_TRUE : VALUE_FALSE;
    vm_error(vm, "empty?: argument must be a collection or nil");
    return VALUE_NIL;
}

/* get: (get map key) or (get map key default) */
static Value native_get(VM* vm, int argc, Value* argv) {
    if (argc < 2 || argc > 3) {
        vm_error(vm, "get: requires 2 or 3 arguments");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    Value key = argv[1];
    Value default_val = (argc == 3) ? argv[2] : VALUE_NIL;

    if (is_nil(coll)) { if (is_pointer(default_val)) object_retain(default_val); return default_val; }

    if (is_hashmap(coll)) {
        Value v = hashmap_get_default(coll, key, default_val);
        if (is_pointer(v)) object_retain(v);
        return v;
    }

    /* Vectors support integer index lookup */
    if (is_vector(coll) && is_fixnum(key)) {
        int64_t idx = untag_fixnum(key);
        if (idx >= 0 && (size_t)idx < vector_length(coll)) {
            Value v = vector_get(coll, (size_t)idx);
            if (is_pointer(v)) object_retain(v);
            return v;
        }
        if (is_pointer(default_val)) object_retain(default_val);
        return default_val;
    }

    vm_error(vm, "get: first argument must be a map, vector, or nil");
    return VALUE_NIL;
}

/* assoc: (assoc map k v ...) => new map with k->v added */
static Value native_assoc(VM* vm, int argc, Value* argv) {
    if (argc < 3 || (argc - 1) % 2 != 0) {
        vm_error(vm, "assoc: requires a map and even number of key-value args");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    if (!is_hashmap(coll) && !is_nil(coll)) {
        vm_error(vm, "assoc: first argument must be a map or nil");
        return VALUE_NIL;
    }
    Value result = is_nil(coll) ? hashmap_create_default() : coll;
    if (!is_nil(coll)) object_retain(result);
    for (int i = 1; i < argc; i += 2) {
        Value next = hashmap_assoc(result, argv[i], argv[i + 1]);
        object_release(result);
        result = next;
    }
    return result;
}

/* dissoc: (dissoc map k ...) => new map with keys removed */
static Value native_dissoc(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, "dissoc: requires at least 2 arguments");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    if (!is_hashmap(coll)) {
        vm_error(vm, "dissoc: first argument must be a map");
        return VALUE_NIL;
    }
    Value result = coll;
    object_retain(result);
    for (int i = 1; i < argc; i++) {
        Value next = hashmap_dissoc(result, argv[i]);
        object_release(result);
        result = next;
    }
    return result;
}

/* keys: (keys map) => list of keys */
static Value native_keys(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "keys: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (is_nil(argv[0])) return VALUE_NIL;
    if (!is_hashmap(argv[0])) {
        vm_error(vm, "keys: argument must be a map");
        return VALUE_NIL;
    }
    Value keys_vec = hashmap_keys(argv[0]);
    Value result = vector_to_list(keys_vec);
    object_release(keys_vec);
    return result;
}

/* vals: (vals map) => list of values */
static Value native_vals(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "vals: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (is_nil(argv[0])) return VALUE_NIL;
    if (!is_hashmap(argv[0])) {
        vm_error(vm, "vals: argument must be a map");
        return VALUE_NIL;
    }
    Value vals_vec = hashmap_values(argv[0]);
    Value result = vector_to_list(vals_vec);
    object_release(vals_vec);
    return result;
}

/* contains?: (contains? map key) => true if map has key */
static Value native_contains_q(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "contains?: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    Value coll = argv[0];
    Value key = argv[1];
    if (is_nil(coll)) return VALUE_FALSE;
    if (is_hashmap(coll)) {
        return hashmap_contains(coll, key) ? VALUE_TRUE : VALUE_FALSE;
    }
    /* Vectors: contains? checks if index is valid */
    if (is_vector(coll) && is_fixnum(key)) {
        int64_t idx = untag_fixnum(key);
        return (idx >= 0 && (size_t)idx < vector_length(coll)) ? VALUE_TRUE : VALUE_FALSE;
    }
    vm_error(vm, "contains?: first argument must be a map or vector");
    return VALUE_NIL;
}

/* concat: (concat & seqs) => concatenate lists */
static Value native_concat(VM* vm, int argc, Value* argv) {
    (void)vm;
    Value result = VALUE_NIL;
    for (int i = 0; i < argc; i++) {
        Value seq = argv[i];
        if (is_nil(seq)) continue;

        /* Convert vectors to lists */
        Value lst;
        bool needs_release = false;
        if (is_cons(seq)) {
            lst = seq;
        } else if (is_vector(seq)) {
            lst = vector_to_list(seq);
            needs_release = true;
        } else {
            if (is_pointer(result)) object_release(result);
            vm_error(vm, "concat: arguments must be sequences");
            return VALUE_NIL;
        }

        if (is_nil(result)) {
            result = lst;
            if (!needs_release && is_pointer(result)) object_retain(result);
        } else {
            Value appended = list_append(result, lst);
            object_release(result);
            if (needs_release) object_release(lst);
            result = appended;
        }
    }
    return result;
}

/* =================================================================
 * Type Predicate Functions
 * ================================================================= */

static Value native_nil_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "nil?: requires exactly 1 argument"); return VALUE_NIL; }
    return is_nil(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_number_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "number?: requires exactly 1 argument"); return VALUE_NIL; }
    return (is_fixnum(argv[0]) || is_float(argv[0]) || is_bigint(argv[0])) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_string_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "string?: requires exactly 1 argument"); return VALUE_NIL; }
    return (is_pointer(argv[0]) && object_type(argv[0]) == TYPE_STRING) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_symbol_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "symbol?: requires exactly 1 argument"); return VALUE_NIL; }
    return (is_pointer(argv[0]) && object_type(argv[0]) == TYPE_SYMBOL) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_keyword_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "keyword?: requires exactly 1 argument"); return VALUE_NIL; }
    return (is_pointer(argv[0]) && object_type(argv[0]) == TYPE_KEYWORD) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_list_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "list?: requires exactly 1 argument"); return VALUE_NIL; }
    return (is_nil(argv[0]) || is_cons(argv[0])) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_vector_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "vector?: requires exactly 1 argument"); return VALUE_NIL; }
    return is_vector(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_map_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "map?: requires exactly 1 argument"); return VALUE_NIL; }
    return is_hashmap(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_fn_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "fn?: requires exactly 1 argument"); return VALUE_NIL; }
    return (is_function(argv[0]) || is_native_function(argv[0])) ? VALUE_TRUE : VALUE_FALSE;
}

/* =================================================================
 * Utility Functions
 * ================================================================= */

/* not: (not x) => boolean negation (nil and false are falsy) */
static Value native_not(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "not: requires exactly 1 argument");
        return VALUE_NIL;
    }
    /* nil and false are falsy, everything else is truthy */
    return (is_nil(argv[0]) || is_false(argv[0])) ? VALUE_TRUE : VALUE_FALSE;
}

/* Helper: append string representation of a value to a buffer.
 * Returns new length. Buffer is reallocated as needed. */
static size_t value_sprint(Value v, char** buf, size_t* cap, size_t len) {
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
        slen = (size_t)utf8_encode(c, tmp);
        tmp[slen] = '\0';
        s = tmp;
    } else if (is_pointer(v)) {
        uint8_t type = object_type(v);
        switch (type) {
            case TYPE_STRING:
                s = string_cstr(v);
                slen = string_byte_length(v);
                break;
            case TYPE_SYMBOL:
                s = symbol_name(v);
                slen = strlen(s);
                break;
            case TYPE_KEYWORD:
                /* Include the leading colon */
                slen = (size_t)snprintf(tmp, sizeof(tmp), ":%s", keyword_name(v));
                s = tmp;
                break;
            case TYPE_BIGINT: {
                /* bigint_print writes to stdout; we need a string.
                 * Use a temporary approach: print to a temp buffer via snprintf/bigint_to_str
                 * For now, fall through to default which is the type name */
                s = "#<bigint>"; slen = 9;
                break;
            }
            default:
                /* For complex types, just use the type name */
                s = value_type_name(v);
                slen = strlen(s);
                break;
        }
    } else {
        s = "#<unknown>"; slen = 10;
    }

    /* Ensure capacity */
    while (len + slen + 1 > *cap) {
        *cap = (*cap < 64) ? 64 : *cap * 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + len, s, slen);
    return len + slen;
}

/* str: (str & args) => concatenate string representations */
static Value native_str(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc == 0) return string_from_cstr("");

    size_t cap = 64;
    char* buf = malloc(cap);
    size_t len = 0;

    for (int i = 0; i < argc; i++) {
        len = value_sprint(argv[i], &buf, &cap, len);
    }
    buf[len] = '\0';

    Value result = string_from_buffer(buf, len);
    free(buf);
    return result;
}

/* symbol: (symbol str) => create a symbol from a string */
static Value native_symbol(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "symbol: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_pointer(argv[0]) || object_type(argv[0]) != TYPE_STRING) {
        vm_error(vm, "symbol: argument must be a string");
        return VALUE_NIL;
    }
    const char* name = string_cstr(argv[0]);
    Value s = symbol_intern(name);
    if (is_pointer(s)) object_retain(s);
    return s;
}

/* gensym: (gensym) or (gensym prefix) => unique symbol */
static int gensym_counter = 0;
static Value native_gensym(VM* vm, int argc, Value* argv) {
    (void)vm;
    const char* prefix = "G__";
    if (argc == 1 && is_pointer(argv[0]) && object_type(argv[0]) == TYPE_STRING) {
        prefix = string_cstr(argv[0]);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s%d", prefix, gensym_counter++);
    Value s = symbol_intern(buf);
    if (is_pointer(s)) object_retain(s);
    return s;
}

/* type: (type x) => keyword describing the type */
static Value native_type(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "type: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value v = argv[0];
    const char* name;

    if (is_nil(v))          name = "nil";
    else if (is_bool(v))    name = "boolean";
    else if (is_fixnum(v))  name = "fixnum";
    else if (is_float(v))   name = "float";
    else if (is_char(v))    name = "char";
    else if (is_pointer(v)) {
        uint8_t type = object_type(v);
        switch (type) {
            case TYPE_BIGINT:    name = "bigint";    break;
            case TYPE_STRING:    name = "string";    break;
            case TYPE_SYMBOL:    name = "symbol";    break;
            case TYPE_KEYWORD:   name = "keyword";   break;
            case TYPE_CONS:      name = "list";      break;
            case TYPE_VECTOR:    name = "vector";    break;
            case TYPE_HASHMAP:   name = "map";       break;
            case TYPE_FUNCTION:  name = "function";  break;
            case TYPE_NATIVE_FN: name = "function";  break;
            default:             name = "unknown";   break;
        }
    } else {
        name = "unknown";
    }

    Value kw = keyword_intern(name);
    if (is_pointer(kw)) object_retain(kw);
    return kw;
}

/* in-ns: (in-ns 'ns-name) => switch to namespace (create if needed) */
static Value native_in_ns(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "in-ns: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value sym = argv[0];
    if (!is_pointer(sym) || object_type(sym) != TYPE_SYMBOL) {
        vm_error(vm, "in-ns: argument must be a symbol");
        return VALUE_NIL;
    }

    const char* name = symbol_name(sym);
    Namespace* ns = namespace_registry_get_or_create(global_namespace_registry, name);
    if (!ns) {
        vm_error(vm, "in-ns: failed to create namespace");
        return VALUE_NIL;
    }
    namespace_registry_set_current(global_namespace_registry, ns);

    /* Update *ns* in beer.core */
    Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
    if (core_ns) {
        Value ns_sym_var = symbol_intern("*ns*");
        Value ns_name_sym = symbol_intern(name);
        namespace_define(core_ns, ns_sym_var, ns_name_sym);
    }

    if (is_pointer(sym)) object_retain(sym);
    return sym;
}

/* require: (require 'foo.bar) or (require 'foo.bar :as 'fb)
 * Loads a namespace from file if not already loaded, optionally aliases it. */
/* Circular require detection */
#define MAX_REQUIRE_DEPTH 64
static const char* requiring_stack[MAX_REQUIRE_DEPTH];
static int requiring_depth = 0;

static bool is_currently_requiring(const char* ns_name) {
    for (int i = 0; i < requiring_depth; i++) {
        if (strcmp(requiring_stack[i], ns_name) == 0) return true;
    }
    return false;
}

static Value native_require(VM* vm, int argc, Value* argv) {
    if (argc != 1 && argc != 3) {
        vm_error(vm, "require: usage (require 'ns) or (require 'ns :as 'alias)");
        return VALUE_NIL;
    }

    Value ns_sym = argv[0];
    if (!is_pointer(ns_sym) || object_type(ns_sym) != TYPE_SYMBOL) {
        vm_error(vm, "require: first argument must be a symbol");
        return VALUE_NIL;
    }

    Value alias_sym = VALUE_NIL;
    if (argc == 3) {
        /* Check :as keyword */
        if (object_type(argv[1]) != TYPE_KEYWORD ||
            strcmp(keyword_name(argv[1]), "as") != 0) {
            vm_error(vm, "require: expected :as keyword");
            return VALUE_NIL;
        }
        alias_sym = argv[2];
        if (!is_pointer(alias_sym) || object_type(alias_sym) != TYPE_SYMBOL) {
            vm_error(vm, "require: alias must be a symbol");
            return VALUE_NIL;
        }
    }

    const char* ns_name = symbol_str(ns_sym);

    /* Check *loaded-libs* (core ns always owns this) */
    Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
    Namespace* current_ns = namespace_registry_current(global_namespace_registry);
    Value loaded_sym = symbol_intern("*loaded-libs*");
    Var* loaded_var = namespace_lookup(core_ns, loaded_sym);
    Value loaded_libs = loaded_var ? var_get_value(loaded_var) : VALUE_NIL;

    Value ns_key = keyword_intern(ns_name);
    bool already_loaded = false;
    if (!is_nil(loaded_libs)) {
        Value check = hashmap_get(loaded_libs, ns_key);
        if (!is_nil(check)) already_loaded = true;
    }

    if (!already_loaded) {
        /* Build file path: dot → slash + .beer */
        char rel_path[512];
        size_t j = 0;
        for (size_t i = 0; ns_name[i] && j < sizeof(rel_path) - 6; i++) {
            rel_path[j++] = (ns_name[i] == '.') ? '/' : ns_name[i];
        }
        rel_path[j] = '\0';
        strcat(rel_path, ".beer");

        /* Search *load-path* — check current ns, then beer.core, then user */
        Value lp_sym = symbol_intern("*load-path*");
        Var* lp_var = namespace_lookup(current_ns, lp_sym);
        if (!lp_var && core_ns) lp_var = namespace_lookup(core_ns, lp_sym);
        if (!lp_var) {
            Namespace* user_ns = namespace_registry_get(global_namespace_registry, "user");
            if (user_ns) lp_var = namespace_lookup(user_ns, lp_sym);
        }
        Value load_path = lp_var ? var_get_value(lp_var) : VALUE_NIL;

        bool found = false;
        char full_path[1024];

        if (!is_nil(load_path)) {
            size_t n = vector_length(load_path);
            for (size_t i = 0; i < n; i++) {
                Value dir = vector_get(load_path, i);
                if (!is_string(dir)) continue;
                snprintf(full_path, sizeof(full_path), "%s%s", string_cstr(dir), rel_path);
                FILE* f = fopen(full_path, "r");
                if (f) {
                    fclose(f);
                    found = true;
                    break;
                }
            }
        }

        /* Fallback: check tar index */
        TarEntry* tar_entry = NULL;
        if (!found) {
            tar_entry = tar_index_lookup(&global_tar_index, rel_path);
            if (tar_entry) found = true;
        }

        if (!found) {
            char buf[256];
            snprintf(buf, sizeof(buf), "require: cannot find file for '%s'", ns_name);
            vm_error(vm, buf);
            return VALUE_NIL;
        }

        /* Circular require detection */
        if (is_currently_requiring(ns_name)) {
            char buf[256];
            snprintf(buf, sizeof(buf), "require: circular dependency detected for '%s'", ns_name);
            vm_error(vm, buf);
            return VALUE_NIL;
        }
        if (requiring_depth >= MAX_REQUIRE_DEPTH) {
            vm_error(vm, "require: maximum nesting depth exceeded");
            return VALUE_NIL;
        }
        requiring_stack[requiring_depth++] = ns_name;

        /* Save current namespace, switch to target, load, restore */
        Namespace* saved_ns = namespace_registry_current(global_namespace_registry);
        Namespace* target_ns = namespace_registry_get_or_create(global_namespace_registry, ns_name);
        namespace_registry_set_current(global_namespace_registry, target_ns);

        if (tar_entry) {
            char* src = tar_index_read_entry(tar_entry);
            if (!src) {
                namespace_registry_set_current(global_namespace_registry, saved_ns);
                requiring_depth--;
                vm_error(vm, "require: failed to read tar entry");
                return VALUE_NIL;
            }
            load_from_buffer(vm, src, tar_entry->ns_path);
            free(src);
        } else {
            Value path_str = string_from_cstr(full_path);
            Value load_argv[1] = { path_str };
            native_load(vm, 1, load_argv);
            object_release(path_str);
        }

        namespace_registry_set_current(global_namespace_registry, saved_ns);
        requiring_depth--;

        if (vm->error) return VALUE_NIL;

        /* Mark as loaded */
        if (is_nil(loaded_libs)) {
            loaded_libs = hashmap_create_default();
        } else {
            object_retain(loaded_libs);
        }
        hashmap_set(loaded_libs, ns_key, VALUE_TRUE);
        var_set_value(loaded_var, loaded_libs);
        object_release(loaded_libs);
    }

    /* Handle :as alias */
    if (!is_nil(alias_sym)) {
        Namespace* current = namespace_registry_current(global_namespace_registry);
        namespace_add_alias(current, alias_sym, ns_name);
    }

    return VALUE_NIL;
}

/* set-macro!: (set-macro! sym) => marks the var as a macro */
static Value native_set_macro(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "set-macro!: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value sym = argv[0];
    if (!is_pointer(sym) || object_type(sym) != TYPE_SYMBOL) {
        vm_error(vm, "set-macro!: argument must be a symbol");
        return VALUE_NIL;
    }

    Namespace* ns = namespace_registry_current(global_namespace_registry);
    if (!ns) {
        vm_error(vm, "set-macro!: no current namespace");
        return VALUE_NIL;
    }
    Var* var = namespace_lookup(ns, sym);
    if (!var) {
        vm_error(vm, "set-macro!: var not found");
        return VALUE_NIL;
    }
    var->is_macro = true;
    return VALUE_NIL;
}

/* apply: (apply f args) or (apply f a b ... args)
 * Last argument must be a sequence; preceding args are prepended */
static Value native_apply(VM* vm, int argc, Value* argv) {
    if (argc < 2) {
        vm_error(vm, "apply: requires at least 2 arguments");
        return VALUE_NIL;
    }

    Value fn = argv[0];
    if (!is_function(fn) && !is_native_function(fn)) {
        vm_error(vm, "apply: first argument must be a function");
        return VALUE_NIL;
    }

    /* Collect all args: argv[1..argc-2] are fixed, argv[argc-1] is a sequence */
    Value seq = argv[argc - 1];
    int fixed_count = argc - 2; /* number of fixed args between fn and seq */

    /* Count total args */
    int seq_count = 0;
    if (is_nil(seq)) {
        seq_count = 0;
    } else if (is_cons(seq)) {
        int64_t n = list_length(seq);
        if (n < 0) {
            vm_error(vm, "apply: last argument must be a proper list");
            return VALUE_NIL;
        }
        seq_count = (int)n;
    } else if (is_vector(seq)) {
        seq_count = (int)vector_length(seq);
    } else {
        vm_error(vm, "apply: last argument must be a sequence");
        return VALUE_NIL;
    }

    int total = fixed_count + seq_count;

    /* Build args array */
    Value* args = NULL;
    if (total > 0) {
        args = malloc(sizeof(Value) * (size_t)total);

        /* Copy fixed args */
        for (int i = 0; i < fixed_count; i++) {
            args[i] = argv[i + 1];
        }

        /* Copy sequence args */
        if (is_cons(seq) || is_nil(seq)) {
            Value cur = seq;
            for (int i = 0; i < seq_count; i++) {
                args[fixed_count + i] = car(cur);
                cur = cdr(cur);
            }
        } else if (is_vector(seq)) {
            for (int i = 0; i < seq_count; i++) {
                args[fixed_count + i] = vector_get(seq, (size_t)i);
            }
        }
    }

    /* Call the function */
    Value result;
    if (is_native_function(fn)) {
        NativeFn native_fn = native_function_ptr(fn);
        result = native_fn(vm, total, args);
    } else {
        /* Bytecode function: build mini bytecode to call it in a temp VM */
        int n_consts = total + 1;  /* args + function */
        Value* consts = malloc(sizeof(Value) * (size_t)n_consts);
        for (int i = 0; i < total; i++) {
            consts[i] = args[i];
        }
        consts[total] = fn;

        /* Bytecode: PUSH_CONST for each arg, PUSH_CONST fn, CALL total, HALT */
        size_t code_size = (size_t)(n_consts * 5 + 3 + 1);
        uint8_t* code = malloc(code_size);
        size_t pc = 0;

        for (int i = 0; i < n_consts; i++) {
            code[pc++] = OP_PUSH_CONST;
            code[pc++] = (uint8_t)(i & 0xFF);
            code[pc++] = (uint8_t)((i >> 8) & 0xFF);
            code[pc++] = (uint8_t)((i >> 16) & 0xFF);
            code[pc++] = (uint8_t)((i >> 24) & 0xFF);
        }

        code[pc++] = OP_CALL;
        code[pc++] = (uint8_t)(total & 0xFF);
        code[pc++] = (uint8_t)((total >> 8) & 0xFF);

        code[pc++] = OP_HALT;

        VM* temp_vm = vm_new(256);
        vm_load_code(temp_vm, code, (int)pc);
        vm_load_constants(temp_vm, consts, n_consts);
        vm_run(temp_vm);

        if (temp_vm->error) {
            char errbuf[256];
            snprintf(errbuf, sizeof(errbuf), "apply: %s", temp_vm->error_msg);
            vm_error(vm, errbuf);
            result = VALUE_NIL;
        } else if (temp_vm->stack_pointer > 0) {
            result = temp_vm->stack[temp_vm->stack_pointer - 1];
            if (is_pointer(result)) object_retain(result);
        } else {
            result = VALUE_NIL;
        }

        vm_free(temp_vm);
        free(code);
        free(consts);
    }

    free(args);
    return result;
}

/* =================================================================
 * String Functions
 * ================================================================= */

/* pr-str: (pr-str & args) => readable string representation */
static Value native_pr_str(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc == 0) return string_from_cstr("");

    size_t cap = 64;
    char* buf = malloc(cap);
    size_t len = 0;

    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            while (len + 2 > cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = ' ';
        }
        len = value_sprint_readable(argv[i], &buf, &cap, len);
    }
    buf[len] = '\0';

    Value result = string_from_buffer(buf, len);
    free(buf);
    return result;
}

/* prn: (prn & args) => print readable, then newline, return nil */
static Value native_prn(VM* vm, int argc, Value* argv) {
    (void)vm;
    Value out = stream_get_stdout();
    for (int i = 0; i < argc; i++) {
        if (i > 0) stream_write_string(out, " ", 1);
        stream_write_value(out, argv[i], true);
    }
    stream_write_string(out, "\n", 1);
    stream_flush(out);
    return VALUE_NIL;
}

/* read-string: (read-string s) => parse one form from a string */
static Value native_read_string(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "read-string: requires 1 string argument");
        return VALUE_NIL;
    }
    const char* src = string_cstr(argv[0]);
    Reader* reader = reader_new(src, "<read-string>");
    Value result = reader_read(reader);
    if (reader_has_error(reader)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "read-string: %s", reader_error_msg(reader));
        reader_free(reader);
        vm_error(vm, buf);
        return VALUE_NIL;
    }
    reader_free(reader);
    return result;
}

/* =================================================================
 * disasm / asm — bytecode metaprogramming
 * ================================================================= */

/* Kept-alive bytecode/constants from asm (same arrays as load) */
#define MAX_ASM_UNITS 256
static uint8_t* asm_bytecodes[MAX_ASM_UNITS];
static Value*   asm_constants[MAX_ASM_UNITS];
static int n_asm_units = 0;

static int find_label_idx(size_t* label_pcs, int n_labels, size_t target) {
    for (int i = 0; i < n_labels; i++) {
        if (label_pcs[i] == target) return i;
    }
    return -1;
}

static bool resolve_label(const char* name, const char** label_names,
                           size_t* label_pcs, int n_labels, size_t* out_pc) {
    for (int i = 0; i < n_labels; i++) {
        if (strcmp(label_names[i], name) == 0) {
            *out_pc = label_pcs[i];
            return true;
        }
    }
    return false;
}

/* disasm: (disasm fn) => {:code [...] :constants [...] :arity N} */
static Value native_disasm(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "disasm: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_function(argv[0])) {
        vm_error(vm, "disasm: argument must be a bytecode function");
        return VALUE_NIL;
    }

    uint8_t* code = function_get_code(argv[0]);
    int code_size = function_get_code_size(argv[0]);
    Value* constants = function_get_constants(argv[0]);
    int num_constants = function_get_num_constants(argv[0]);
    uint32_t code_offset = function_code_offset(argv[0]);
    int arity = function_arity(argv[0]);

    if (!code || code_size <= 0) {
        vm_error(vm, "disasm: function has no bytecode");
        return VALUE_NIL;
    }

    /* Pass 1: collect jump targets → assign labels */
    #define MAX_LABELS 256
    size_t label_pcs[MAX_LABELS];
    int n_labels = 0;

    size_t pc = code_offset;
    while (pc < (size_t)code_size) {
        uint8_t op = code[pc];
        const OpcodeInfo* info = opcode_info_by_value(op);
        if (!info) break;

        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE) {
            /* Relative int32 offset after the operand */
            int32_t offset = 0;
            if (pc + 5 <= (size_t)code_size) {
                for (int i = 0; i < 4; i++)
                    offset |= ((int32_t)code[pc + 1 + i]) << (i * 8);
            }
            size_t target = pc + 5 + (size_t)offset;
            /* Add to labels if not already there */
            bool found = false;
            for (int i = 0; i < n_labels; i++) {
                if (label_pcs[i] == target) { found = true; break; }
            }
            if (!found && n_labels < MAX_LABELS)
                label_pcs[n_labels++] = target;
        } else if (op == OP_PUSH_HANDLER) {
            /* Absolute uint32 PC */
            uint32_t target = 0;
            if (pc + 5 <= (size_t)code_size) {
                for (int i = 0; i < 4; i++)
                    target |= ((uint32_t)code[pc + 1 + i]) << (i * 8);
            }
            bool found = false;
            for (int i = 0; i < n_labels; i++) {
                if (label_pcs[i] == (size_t)target) { found = true; break; }
            }
            if (!found && n_labels < MAX_LABELS)
                label_pcs[n_labels++] = (size_t)target;
        }

        pc += (size_t)info->total_size;
    }

    /* Sort labels by PC for consistent naming */
    for (int i = 0; i < n_labels - 1; i++)
        for (int j = i + 1; j < n_labels; j++)
            if (label_pcs[i] > label_pcs[j]) {
                size_t tmp = label_pcs[i];
                label_pcs[i] = label_pcs[j];
                label_pcs[j] = tmp;
            }

    /* Helper: find label index for a PC (inline, not a macro) */

    /* Pass 2: scan bytecode to find which constants are actually used */
    bool used_consts[4096] = {false};
    int max_const_idx = -1;
    pc = code_offset;
    while (pc < (size_t)code_size) {
        uint8_t op = code[pc];
        const OpcodeInfo* info = opcode_info_by_value(op);
        if (!info) break;

        /* Opcodes that reference constants by index */
        if (op == OP_PUSH_CONST) {
            uint32_t idx = 0;
            for (int i = 0; i < 4; i++) idx |= ((uint32_t)code[pc+1+i]) << (i*8);
            if ((int)idx < num_constants && idx < 4096) {
                used_consts[idx] = true;
                if ((int)idx > max_const_idx) max_const_idx = (int)idx;
            }
        } else if (op == OP_LOAD_VAR || op == OP_STORE_VAR) {
            uint16_t idx = 0;
            for (int i = 0; i < 2; i++) idx |= ((uint16_t)code[pc+1+i]) << (i*8);
            if ((int)idx < num_constants && idx < 4096) {
                used_consts[idx] = true;
                if ((int)idx > max_const_idx) max_const_idx = (int)idx;
            }
        } else if (op == OP_MAKE_CLOSURE) {
            /* name_idx at offset 11-12 */
            uint16_t ni = 0;
            for (int i = 0; i < 2; i++) ni |= ((uint16_t)code[pc+11+i]) << (i*8);
            if ((int)ni < num_constants && ni < 4096) {
                used_consts[ni] = true;
                if ((int)ni > max_const_idx) max_const_idx = (int)ni;
            }
        }

        pc += (size_t)info->total_size;
        if (op == OP_RETURN || op == OP_HALT) break;
    }

    /* Build old→new constant index mapping */
    int const_remap[4096];
    int new_num_constants = 0;
    for (int i = 0; i <= max_const_idx && i < 4096; i++) {
        if (used_consts[i]) {
            const_remap[i] = new_num_constants++;
        } else {
            const_remap[i] = -1;
        }
    }

    /* Pass 3: build code vector with remapped constant indices */
    Value code_vec = vector_create(32);
    pc = code_offset;

    while (pc < (size_t)code_size) {
        /* Insert label if this PC is a target */
        int label_idx = find_label_idx(label_pcs, n_labels, pc);
        if (label_idx >= 0) {
            char lname[16];
            snprintf(lname, sizeof(lname), "L%d", label_idx);
            Value label_instr = vector_create(2);
            Value kw_label = keyword_intern("LABEL");
            Value kw_name = keyword_intern(lname);
            vector_push(label_instr, kw_label);
            vector_push(label_instr, kw_name);
            vector_push(code_vec, label_instr);
            object_release(label_instr);
        }

        uint8_t op = code[pc];
        const OpcodeInfo* info = opcode_info_by_value(op);
        if (!info) break;

        Value instr = vector_create(2);
        Value kw_op = keyword_intern(info->name);
        vector_push(instr, kw_op);

        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE) {
            int32_t offset = 0;
            for (int i = 0; i < 4; i++)
                offset |= ((int32_t)code[pc + 1 + i]) << (i * 8);
            size_t target = pc + 5 + (size_t)offset;
            int li = find_label_idx(label_pcs, n_labels, target);
            if (li >= 0) {
                char lname[16];
                snprintf(lname, sizeof(lname), "L%d", li);
                Value kw = keyword_intern(lname);
                vector_push(instr, kw);
            } else {
                vector_push(instr, make_fixnum(offset));
            }
        } else if (op == OP_PUSH_HANDLER) {
            uint32_t target = 0;
            for (int i = 0; i < 4; i++)
                target |= ((uint32_t)code[pc + 1 + i]) << (i * 8);
            int li = find_label_idx(label_pcs, n_labels, (size_t)target);
            if (li >= 0) {
                char lname[16];
                snprintf(lname, sizeof(lname), "L%d", li);
                Value kw = keyword_intern(lname);
                vector_push(instr, kw);
            } else {
                vector_push(instr, make_fixnum((int64_t)target));
            }
        } else if (op == OP_MAKE_CLOSURE) {
            uint32_t co = 0;
            for (int i = 0; i < 4; i++) co |= ((uint32_t)code[pc+1+i]) << (i*8);
            uint16_t nl = 0, nc = 0, ar = 0, ni = 0;
            for (int i = 0; i < 2; i++) nl |= ((uint16_t)code[pc+5+i]) << (i*8);
            for (int i = 0; i < 2; i++) nc |= ((uint16_t)code[pc+7+i]) << (i*8);
            for (int i = 0; i < 2; i++) ar |= ((uint16_t)code[pc+9+i]) << (i*8);
            for (int i = 0; i < 2; i++) ni |= ((uint16_t)code[pc+11+i]) << (i*8);
            vector_push(instr, make_fixnum((int64_t)co));
            vector_push(instr, make_fixnum((int64_t)nl));
            vector_push(instr, make_fixnum((int64_t)nc));
            vector_push(instr, make_fixnum((int16_t)ar));
            /* Remap name_idx */
            int remapped_ni = (ni < 4096 && used_consts[ni]) ? const_remap[ni] : (int)ni;
            vector_push(instr, make_fixnum((int64_t)remapped_ni));
        } else if (op == OP_PUSH_INT) {
            int64_t val = 0;
            for (int i = 0; i < 8; i++)
                val |= ((int64_t)code[pc + 1 + i]) << (i * 8);
            vector_push(instr, make_fixnum(val));
        } else if (op == OP_PUSH_CONST) {
            /* Remap constant index */
            uint32_t old_idx = 0;
            for (int i = 0; i < 4; i++)
                old_idx |= ((uint32_t)code[pc + 1 + i]) << (i * 8);
            int new_idx = (old_idx < 4096 && used_consts[old_idx]) ? const_remap[old_idx] : (int)old_idx;
            vector_push(instr, make_fixnum((int64_t)new_idx));
        } else if (op == OP_LOAD_VAR || op == OP_STORE_VAR) {
            /* Remap constant index (uint16) */
            uint16_t old_idx = 0;
            for (int i = 0; i < 2; i++)
                old_idx |= ((uint16_t)code[pc + 1 + i]) << (i * 8);
            int new_idx = (old_idx < 4096 && used_consts[old_idx]) ? const_remap[old_idx] : (int)old_idx;
            vector_push(instr, make_fixnum((int64_t)new_idx));
        } else if (info->total_size == 3) {
            /* uint16 operand (non-var) */
            uint16_t operand = 0;
            for (int i = 0; i < 2; i++)
                operand |= ((uint16_t)code[pc + 1 + i]) << (i * 8);
            vector_push(instr, make_fixnum((int64_t)operand));
        } else if (info->total_size == 5) {
            /* uint32 operand (non-PUSH_CONST) */
            uint32_t operand = 0;
            for (int i = 0; i < 4; i++)
                operand |= ((uint32_t)code[pc + 1 + i]) << (i * 8);
            vector_push(instr, make_fixnum((int64_t)operand));
        }
        /* total_size == 1: no operand, nothing to push */

        vector_push(code_vec, instr);
        object_release(instr);
        pc += (size_t)info->total_size;

        /* Stop after the function's RETURN (first RETURN after ENTER) */
        if (op == OP_RETURN || op == OP_HALT) break;
    }

    /* Insert any remaining label at the end */
    {
        int label_idx = find_label_idx(label_pcs, n_labels, pc);
        if (label_idx >= 0) {
            char lname[16];
            snprintf(lname, sizeof(lname), "L%d", label_idx);
            Value label_instr = vector_create(2);
            vector_push(label_instr, keyword_intern("LABEL"));
            vector_push(label_instr, keyword_intern(lname));
            vector_push(code_vec, label_instr);
            object_release(label_instr);
        }
    }


    /* Build constants vector — only include used constants */
    Value const_vec = vector_create(new_num_constants > 0 ? (size_t)new_num_constants : 1);
    for (int i = 0; i <= max_const_idx && i < 4096; i++) {
        if (!used_consts[i]) continue;
        Value c = constants[i];
        if (is_pointer(c)) object_retain(c);
        vector_push(const_vec, c);
        if (is_pointer(c)) object_release(c);
    }

    /* Build result map */
    Value result = hashmap_create_default();
    hashmap_set(result, keyword_intern("code"), code_vec);
    hashmap_set(result, keyword_intern("constants"), const_vec);
    hashmap_set(result, keyword_intern("arity"), make_fixnum((int64_t)arity));
    object_release(code_vec);
    object_release(const_vec);

    return result;
}

/* asm: (asm {:code [...] :constants [...] :arity N}) => function */
static Value native_asm(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "asm: requires exactly 1 argument (a map)");
        return VALUE_NIL;
    }
    if (!is_pointer(argv[0]) || object_type(argv[0]) != TYPE_HASHMAP) {
        vm_error(vm, "asm: argument must be a map with :code, :constants, :arity");
        return VALUE_NIL;
    }

    Value map = argv[0];
    Value code_val = hashmap_get(map, keyword_intern("code"));
    Value const_val = hashmap_get(map, keyword_intern("constants"));
    Value arity_val = hashmap_get(map, keyword_intern("arity"));

    if (is_nil(code_val) || !is_pointer(code_val) || object_type(code_val) != TYPE_VECTOR) {
        vm_error(vm, "asm: :code must be a vector of instruction vectors");
        return VALUE_NIL;
    }
    if (!is_fixnum(arity_val)) {
        vm_error(vm, "asm: :arity must be an integer");
        return VALUE_NIL;
    }

    int arity = (int)untag_fixnum(arity_val);
    size_t n_instrs = vector_length(code_val);

    /* Build constants array */
    int num_constants = 0;
    Value* const_arr = NULL;
    if (!is_nil(const_val) && is_pointer(const_val) && object_type(const_val) == TYPE_VECTOR) {
        num_constants = (int)vector_length(const_val);
        const_arr = malloc((size_t)num_constants * sizeof(Value));
        for (int i = 0; i < num_constants; i++) {
            const_arr[i] = vector_get(const_val, (size_t)i);
            if (is_pointer(const_arr[i])) object_retain(const_arr[i]);
        }
    } else {
        const_arr = malloc(sizeof(Value));
        num_constants = 0;
    }

    /* Pass 1: collect labels and compute PCs */
    #define MAX_ASM_LABELS 256
    struct { const char* name; size_t pc; } labels[MAX_ASM_LABELS];
    int n_labels = 0;
    size_t total_size = 0;

    for (size_t i = 0; i < n_instrs; i++) {
        Value instr = vector_get(code_val, i);
        if (!is_pointer(instr) || object_type(instr) != TYPE_VECTOR || vector_length(instr) < 1) {
            vm_error(vm, "asm: each instruction must be a non-empty vector");
            free(const_arr);
            return VALUE_NIL;
        }
        Value op_kw = vector_get(instr, 0);
        if (!is_pointer(op_kw) || object_type(op_kw) != TYPE_KEYWORD) {
            vm_error(vm, "asm: instruction opcode must be a keyword");
            free(const_arr);
            return VALUE_NIL;
        }
        const char* op_name = keyword_name(op_kw);

        if (strcmp(op_name, "LABEL") == 0) {
            if (vector_length(instr) < 2) {
                vm_error(vm, "asm: :LABEL requires a label name");
                free(const_arr);
                return VALUE_NIL;
            }
            Value label_kw = vector_get(instr, 1);
            if (!is_pointer(label_kw) || object_type(label_kw) != TYPE_KEYWORD) {
                vm_error(vm, "asm: label name must be a keyword");
                free(const_arr);
                return VALUE_NIL;
            }
            if (n_labels >= MAX_ASM_LABELS) {
                vm_error(vm, "asm: too many labels");
                free(const_arr);
                return VALUE_NIL;
            }
            labels[n_labels].name = keyword_name(label_kw);
            labels[n_labels].pc = total_size;
            n_labels++;
            continue;
        }

        const OpcodeInfo* info = opcode_info_by_name(op_name);
        if (!info) {
            char buf[256];
            snprintf(buf, sizeof(buf), "asm: unknown opcode :%s", op_name);
            vm_error(vm, buf);
            free(const_arr);
            return VALUE_NIL;
        }
        total_size += (size_t)info->total_size;
    }

    /* Extract parallel arrays for resolve_label helper */
    const char* label_names_arr[MAX_ASM_LABELS];
    size_t label_pcs_arr[MAX_ASM_LABELS];
    for (int li = 0; li < n_labels; li++) {
        label_names_arr[li] = labels[li].name;
        label_pcs_arr[li] = labels[li].pc;
    }

    /* Pass 2: emit bytecode */
    uint8_t* bytecode = malloc(total_size);
    size_t pc = 0;
    uint16_t n_locals = 0;

    for (size_t i = 0; i < n_instrs; i++) {
        Value instr = vector_get(code_val, i);
        Value op_kw = vector_get(instr, 0);
        const char* op_name = keyword_name(op_kw);

        if (strcmp(op_name, "LABEL") == 0) continue;

        const OpcodeInfo* info = opcode_info_by_name(op_name);
        bytecode[pc] = info->opcode;

        if (info->opcode == OP_ENTER && vector_length(instr) >= 2) {
            n_locals = (uint16_t)untag_fixnum(vector_get(instr, 1));
        }

        if (info->opcode == OP_JUMP || info->opcode == OP_JUMP_IF_FALSE) {
            /* Operand: label keyword → resolve to relative offset */
            if (vector_length(instr) < 2) {
                vm_error(vm, "asm: jump instruction requires a label operand");
                free(bytecode); free(const_arr);
                return VALUE_NIL;
            }
            Value operand = vector_get(instr, 1);
            int32_t offset;
            if (is_pointer(operand) && object_type(operand) == TYPE_KEYWORD) {
                size_t target_pc;
                if (!resolve_label(keyword_name(operand), label_names_arr, label_pcs_arr, n_labels, &target_pc)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "asm: undefined label :%s", keyword_name(operand));
                    vm_error(vm, buf);
                    free(bytecode); free(const_arr);
                    return VALUE_NIL;
                }
                offset = (int32_t)((int64_t)target_pc - (int64_t)(pc + 5));
            } else {
                offset = (int32_t)untag_fixnum(operand);
            }
            for (int b = 0; b < 4; b++)
                bytecode[pc + 1 + b] = (uint8_t)((offset >> (b * 8)) & 0xFF);
        } else if (info->opcode == OP_PUSH_HANDLER) {
            /* Operand: label keyword → resolve to absolute PC */
            if (vector_length(instr) < 2) {
                vm_error(vm, "asm: PUSH_HANDLER requires a label operand");
                free(bytecode); free(const_arr);
                return VALUE_NIL;
            }
            Value operand = vector_get(instr, 1);
            uint32_t target;
            if (is_pointer(operand) && object_type(operand) == TYPE_KEYWORD) {
                size_t target_pc;
                if (!resolve_label(keyword_name(operand), label_names_arr, label_pcs_arr, n_labels, &target_pc)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "asm: undefined label :%s", keyword_name(operand));
                    vm_error(vm, buf);
                    free(bytecode); free(const_arr);
                    return VALUE_NIL;
                }
                target = (uint32_t)target_pc;
            } else {
                target = (uint32_t)untag_fixnum(operand);
            }
            for (int b = 0; b < 4; b++)
                bytecode[pc + 1 + b] = (uint8_t)((target >> (b * 8)) & 0xFF);
        } else if (info->opcode == OP_MAKE_CLOSURE) {
            /* 5 operands: code_offset(u32) n_locals(u16) n_closed(u16) arity(u16) name_idx(u16) */
            if (vector_length(instr) < 6) {
                vm_error(vm, "asm: MAKE_CLOSURE requires 5 operands");
                free(bytecode); free(const_arr);
                return VALUE_NIL;
            }
            uint32_t co = (uint32_t)untag_fixnum(vector_get(instr, 1));
            uint16_t nl = (uint16_t)untag_fixnum(vector_get(instr, 2));
            uint16_t nc = (uint16_t)untag_fixnum(vector_get(instr, 3));
            uint16_t ar = (uint16_t)(int16_t)untag_fixnum(vector_get(instr, 4));
            uint16_t ni = (uint16_t)untag_fixnum(vector_get(instr, 5));
            for (int b = 0; b < 4; b++) bytecode[pc+1+b] = (uint8_t)((co >> (b*8)) & 0xFF);
            for (int b = 0; b < 2; b++) bytecode[pc+5+b] = (uint8_t)((nl >> (b*8)) & 0xFF);
            for (int b = 0; b < 2; b++) bytecode[pc+7+b] = (uint8_t)((nc >> (b*8)) & 0xFF);
            for (int b = 0; b < 2; b++) bytecode[pc+9+b] = (uint8_t)((ar >> (b*8)) & 0xFF);
            for (int b = 0; b < 2; b++) bytecode[pc+11+b] = (uint8_t)((ni >> (b*8)) & 0xFF);
        } else if (info->opcode == OP_PUSH_INT) {
            if (vector_length(instr) < 2) {
                vm_error(vm, "asm: PUSH_INT requires an operand");
                free(bytecode); free(const_arr);
                return VALUE_NIL;
            }
            int64_t val = untag_fixnum(vector_get(instr, 1));
            for (int b = 0; b < 8; b++)
                bytecode[pc + 1 + b] = (uint8_t)((val >> (b * 8)) & 0xFF);
        } else if (info->total_size == 3) {
            /* uint16 operand */
            if (vector_length(instr) < 2) {
                char buf[256];
                snprintf(buf, sizeof(buf), "asm: :%s requires an operand", op_name);
                vm_error(vm, buf);
                free(bytecode); free(const_arr);
                return VALUE_NIL;
            }
            uint16_t operand = (uint16_t)untag_fixnum(vector_get(instr, 1));
            bytecode[pc + 1] = (uint8_t)(operand & 0xFF);
            bytecode[pc + 2] = (uint8_t)((operand >> 8) & 0xFF);
        } else if (info->total_size == 5) {
            /* uint32 operand (PUSH_CONST) */
            if (vector_length(instr) < 2) {
                char buf[256];
                snprintf(buf, sizeof(buf), "asm: :%s requires an operand", op_name);
                vm_error(vm, buf);
                free(bytecode); free(const_arr);
                return VALUE_NIL;
            }
            uint32_t operand = (uint32_t)untag_fixnum(vector_get(instr, 1));
            for (int b = 0; b < 4; b++)
                bytecode[pc + 1 + b] = (uint8_t)((operand >> (b * 8)) & 0xFF);
        }

        pc += (size_t)info->total_size;
    }


    /* Create function object */
    Value fn = function_new(arity, 0, n_locals, "asm-fn");

    /* Set execution context — must keep bytecode+constants alive */
    function_set_code(fn, bytecode, (int)total_size, const_arr, num_constants);

    /* Set namespace */
    Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
    if (cur_ns) function_set_ns_name(fn, cur_ns->name);

    /* Keep bytecode+constants alive for program lifetime */
    if (n_asm_units < MAX_ASM_UNITS) {
        asm_bytecodes[n_asm_units] = bytecode;
        asm_constants[n_asm_units] = const_arr;
        n_asm_units++;
    }

    return fn;
}

/* subs: (subs s start) or (subs s start end) => substring by char index */
static Value native_subs(VM* vm, int argc, Value* argv) {
    if (argc < 2 || argc > 3) {
        vm_error(vm, "subs: requires 2 or 3 arguments");
        return VALUE_NIL;
    }
    if (!is_string(argv[0])) {
        vm_error(vm, "subs: first argument must be a string");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[1])) {
        vm_error(vm, "subs: start must be an integer");
        return VALUE_NIL;
    }
    int64_t start = untag_fixnum(argv[1]);
    if (start < 0) {
        vm_error(vm, "subs: start must be non-negative");
        return VALUE_NIL;
    }
    size_t end;
    if (argc == 3) {
        if (!is_fixnum(argv[2])) {
            vm_error(vm, "subs: end must be an integer");
            return VALUE_NIL;
        }
        int64_t e = untag_fixnum(argv[2]);
        if (e < start) {
            vm_error(vm, "subs: end must be >= start");
            return VALUE_NIL;
        }
        end = (size_t)e;
    } else {
        end = string_char_length(argv[0]);
    }
    Value result = string_subs(argv[0], (size_t)start, end);
    if (is_nil(result)) {
        vm_error(vm, "subs: index out of bounds");
        return VALUE_NIL;
    }
    return result;
}

/* str/upper-case */
static Value native_str_upper(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "str/upper-case: requires exactly 1 string argument");
        return VALUE_NIL;
    }
    return string_upper(argv[0]);
}

/* str/lower-case */
static Value native_str_lower(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "str/lower-case: requires exactly 1 string argument");
        return VALUE_NIL;
    }
    return string_lower(argv[0]);
}

/* str/trim */
static Value native_str_trim(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "str/trim: requires exactly 1 string argument");
        return VALUE_NIL;
    }
    return string_trim(argv[0]);
}

/* str/join: (str/join coll) or (str/join sep coll) */
static Value native_str_join(VM* vm, int argc, Value* argv) {
    if (argc == 1) {
        return string_join(VALUE_NIL, argv[0]);
    }
    if (argc == 2) {
        if (!is_string(argv[0])) {
            vm_error(vm, "str/join: separator must be a string");
            return VALUE_NIL;
        }
        return string_join(argv[0], argv[1]);
    }
    vm_error(vm, "str/join: requires 1 or 2 arguments");
    return VALUE_NIL;
}

/* str/split: (str/split s delim) */
static Value native_str_split(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "str/split: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    if (!is_string(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "str/split: arguments must be strings");
        return VALUE_NIL;
    }
    return string_split(argv[0], argv[1]);
}

/* str/includes? */
static Value native_str_includes(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_string(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "str/includes?: requires 2 string arguments");
        return VALUE_NIL;
    }
    return string_contains(argv[0], argv[1]) ? VALUE_TRUE : VALUE_FALSE;
}

/* str/starts-with? */
static Value native_str_starts_with(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_string(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "str/starts-with?: requires 2 string arguments");
        return VALUE_NIL;
    }
    return string_starts_with(argv[0], argv[1]) ? VALUE_TRUE : VALUE_FALSE;
}

/* str/ends-with? */
static Value native_str_ends_with(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_string(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "str/ends-with?: requires 2 string arguments");
        return VALUE_NIL;
    }
    return string_ends_with(argv[0], argv[1]) ? VALUE_TRUE : VALUE_FALSE;
}

/* str/replace */
static Value native_str_replace(VM* vm, int argc, Value* argv) {
    if (argc != 3 || !is_string(argv[0]) || !is_string(argv[1]) || !is_string(argv[2])) {
        vm_error(vm, "str/replace: requires 3 string arguments");
        return VALUE_NIL;
    }
    return string_replace(argv[0], argv[1], argv[2]);
}

/* char?: (char? x) => true if x is a character */
static Value native_char_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "char?: requires exactly 1 argument"); return VALUE_NIL; }
    return is_char(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

/* =================================================================
 * Helper: register a single native function in user namespace
 * ================================================================= */

static void register_native(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
    /* Don't release sym — symbol_intern returns an unowned reference */
}

/* =================================================================
 * Core Function Registration
 * ================================================================= */

void core_register_arithmetic(void) {
    /* Get the 'user' namespace */
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) {
        fprintf(stderr, "ERROR: Failed to get 'user' namespace\n");
        return;
    }

    /* Create and register native functions (variadic arity = -1) */
    Value add_fn = native_function_new(-1, native_add, "+");
    Value sub_fn = native_function_new(-1, native_sub, "-");
    Value mul_fn = native_function_new(-1, native_mul, "*");
    Value div_fn = native_function_new(-1, native_div, "/");

    /* Register in namespace */
    Value add_sym = symbol_intern("+");
    Value sub_sym = symbol_intern("-");
    Value mul_sym = symbol_intern("*");
    Value div_sym = symbol_intern("/");

    namespace_define(core_ns, add_sym, add_fn);
    namespace_define(core_ns, sub_sym, sub_fn);
    namespace_define(core_ns, mul_sym, mul_fn);
    namespace_define(core_ns, div_sym, div_fn);

    Value mod_fn = native_function_new(2, native_mod, "mod");
    Value rem_fn = native_function_new(2, native_rem, "rem");
    Value mod_sym = symbol_intern("mod");
    Value rem_sym = symbol_intern("rem");
    namespace_define(core_ns, mod_sym, mod_fn);
    namespace_define(core_ns, rem_sym, rem_fn);

    Value quot_fn = native_function_new(2, native_quot, "quot");
    Value quot_sym = symbol_intern("quot");
    namespace_define(core_ns, quot_sym, quot_fn);

    /* Release fn references (namespace owns them now via Vars) */
    object_release(add_fn);
    object_release(sub_fn);
    object_release(mul_fn);
    object_release(div_fn);
    object_release(mod_fn);
    object_release(rem_fn);
    object_release(quot_fn);
    /* Don't release symbols — symbol_intern returns unowned references */
}

void core_register_io(void) {
    /* Get the 'user' namespace */
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) {
        fprintf(stderr, "ERROR: Failed to get 'user' namespace\n");
        return;
    }

    /* Create and register native functions (variadic arity = -1) */
    Value print_fn = native_function_new(-1, native_print, "print");
    Value println_fn = native_function_new(-1, native_println, "println");

    /* Register in namespace */
    Value print_sym = symbol_intern("print");
    Value println_sym = symbol_intern("println");

    namespace_define(core_ns, print_sym, print_fn);
    namespace_define(core_ns, println_sym, println_fn);

    /* Release fn references */
    object_release(print_fn);
    object_release(println_fn);
}

void core_register_comparison(void) {
    /* Get the 'user' namespace */
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) {
        fprintf(stderr, "ERROR: Failed to get 'user' namespace\n");
        return;
    }

    /* Create and register native functions (variadic arity = -1) */
    Value eq_fn  = native_function_new(-1, native_eq,  "=");
    Value lt_fn  = native_function_new(-1, native_lt,  "<");
    Value gt_fn  = native_function_new(-1, native_gt,  ">");
    Value lte_fn = native_function_new(-1, native_lte, "<=");
    Value gte_fn = native_function_new(-1, native_gte, ">=");

    /* Register in namespace */
    Value eq_sym  = symbol_intern("=");
    Value lt_sym  = symbol_intern("<");
    Value gt_sym  = symbol_intern(">");
    Value lte_sym = symbol_intern("<=");
    Value gte_sym = symbol_intern(">=");

    namespace_define(core_ns, eq_sym, eq_fn);
    namespace_define(core_ns, lt_sym, lt_fn);
    namespace_define(core_ns, gt_sym, gt_fn);
    namespace_define(core_ns, lte_sym, lte_fn);
    namespace_define(core_ns, gte_sym, gte_fn);

    /* Release fn references */
    object_release(eq_fn);
    object_release(lt_fn);
    object_release(gt_fn);
    object_release(lte_fn);
    object_release(gte_fn);
}

void core_register_collections(void) {
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) {
        fprintf(stderr, "ERROR: Failed to get 'user' namespace\n");
        return;
    }

    register_native(core_ns, "list", native_list);
    register_native(core_ns, "vector", native_vector);
    register_native(core_ns, "hash-map", native_hash_map);
    register_native(core_ns, "cons", native_cons);
    register_native(core_ns, "first", native_first);
    register_native(core_ns, "rest", native_rest);
    register_native(core_ns, "nth", native_nth);
    register_native(core_ns, "count", native_count);
    register_native(core_ns, "conj", native_conj);
    register_native(core_ns, "empty?", native_empty_q);
    register_native(core_ns, "get", native_get);
    register_native(core_ns, "assoc", native_assoc);
    register_native(core_ns, "dissoc", native_dissoc);
    register_native(core_ns, "keys", native_keys);
    register_native(core_ns, "vals", native_vals);
    register_native(core_ns, "contains?", native_contains_q);
    register_native(core_ns, "concat", native_concat);
}

void core_register_predicates(void) {
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) {
        fprintf(stderr, "ERROR: Failed to get 'user' namespace\n");
        return;
    }

    register_native(core_ns, "nil?", native_nil_q);
    register_native(core_ns, "number?", native_number_q);
    register_native(core_ns, "string?", native_string_q);
    register_native(core_ns, "symbol?", native_symbol_q);
    register_native(core_ns, "keyword?", native_keyword_q);
    register_native(core_ns, "list?", native_list_q);
    register_native(core_ns, "vector?", native_vector_q);
    register_native(core_ns, "map?", native_map_q);
    register_native(core_ns, "fn?", native_fn_q);
    register_native(core_ns, "char?", native_char_q);
    register_native(core_ns, "float?", native_float_q);
    register_native(core_ns, "int?", native_int_q);
}

/* Forward declaration — defined after streams section */
Value native_ns_publics(VM* vm, int argc, Value* argv);

void core_register_utility(void) {
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) {
        fprintf(stderr, "ERROR: Failed to get 'user' namespace\n");
        return;
    }

    register_native(core_ns, "not", native_not);
    register_native(core_ns, "str", native_str);
    register_native(core_ns, "symbol", native_symbol);
    register_native(core_ns, "gensym", native_gensym);
    register_native(core_ns, "type", native_type);
    register_native(core_ns, "float", native_float);
    register_native(core_ns, "int", native_int);
    register_native(core_ns, "apply", native_apply);
    register_native(core_ns, "set-macro!", native_set_macro);
    register_native(core_ns, "macroexpand-1", native_macroexpand_1);
    register_native(core_ns, "macroexpand", native_macroexpand);
    register_native(core_ns, "in-ns", native_in_ns);
    register_native(core_ns, "load", native_load);
    register_native(core_ns, "pr-str", native_pr_str);
    register_native(core_ns, "prn", native_prn);
    register_native(core_ns, "read-string", native_read_string);
    register_native(core_ns, "disasm", native_disasm);
    register_native(core_ns, "asm", native_asm);
    register_native(core_ns, "subs", native_subs);
    register_native(core_ns, "str/upper-case", native_str_upper);
    register_native(core_ns, "str/lower-case", native_str_lower);
    register_native(core_ns, "str/trim", native_str_trim);
    register_native(core_ns, "str/join", native_str_join);
    register_native(core_ns, "str/split", native_str_split);
    register_native(core_ns, "str/includes?", native_str_includes);
    register_native(core_ns, "str/starts-with?", native_str_starts_with);
    register_native(core_ns, "str/ends-with?", native_str_ends_with);
    register_native(core_ns, "str/replace", native_str_replace);
    register_native(core_ns, "require", native_require);
    register_native(core_ns, "ns-publics", native_ns_publics);

    /* Initialize *loaded-libs* as empty map */
    Value loaded_sym = symbol_intern("*loaded-libs*");
    Value empty_map = hashmap_create_default();
    namespace_define(core_ns, loaded_sym, empty_map);
    object_release(empty_map);

    /* Initialize *load-path* — BEERPATH (colon-separated) first,
       then BEER_LIB_PATH (single dir, legacy), then "lib/" */
    Value lp_sym = symbol_intern("*load-path*");
    Value lp_vec = vector_create(4);
    const char* beerpath_env = getenv("BEERPATH");
    const char* beer_lib_env = getenv("BEER_LIB_PATH");
    if (beerpath_env) {
        /* Split BEERPATH on ':' and add each directory */
        char beerpath_copy[4096];
        snprintf(beerpath_copy, sizeof(beerpath_copy), "%s", beerpath_env);
        char* saveptr = NULL;
        char* token = strtok_r(beerpath_copy, ":", &saveptr);
        while (token) {
            if (token[0] != '\0') {
                char buf[1024];
                size_t len = strlen(token);
                if (len > 0 && token[len-1] == '/') {
                    snprintf(buf, sizeof(buf), "%s", token);
                } else {
                    snprintf(buf, sizeof(buf), "%s/", token);
                }
                Value env_str = string_from_cstr(buf);
                vector_push(lp_vec, env_str);
                object_release(env_str);
            }
            token = strtok_r(NULL, ":", &saveptr);
        }
    } else if (beer_lib_env) {
        /* Legacy: single directory from BEER_LIB_PATH */
        char buf[1024];
        size_t len = strlen(beer_lib_env);
        if (len > 0 && beer_lib_env[len-1] == '/') {
            snprintf(buf, sizeof(buf), "%s", beer_lib_env);
        } else {
            snprintf(buf, sizeof(buf), "%s/", beer_lib_env);
        }
        Value env_str = string_from_cstr(buf);
        vector_push(lp_vec, env_str);
        object_release(env_str);
    }
    Value lib_str = string_from_cstr("lib/");
    vector_push(lp_vec, lib_str);
    object_release(lib_str);
    namespace_define(core_ns, lp_sym, lp_vec);
    object_release(lp_vec);

    /* Scan BEERPATH directories for .tar files */
    tar_index_init(&global_tar_index);
    {
        size_t n = vector_length(lp_vec);
        for (size_t i = 0; i < n; i++) {
            Value dir = vector_get(lp_vec, i);
            if (is_string(dir)) {
                tar_index_scan_dir(&global_tar_index, string_cstr(dir));
            }
        }
    }

    /* Initialize *ns* to 'user (the default current namespace) */
    Value ns_var_sym = symbol_intern("*ns*");
    Value user_sym = symbol_intern("user");
    namespace_define(core_ns, ns_var_sym, user_sym);
}

/* =================================================================
 * Stream / I/O Functions
 * ================================================================= */

/* open: (open path mode) — mode is :read, :write, :append */
static Value native_open(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "open: requires 2 arguments (path mode)");
        return VALUE_NIL;
    }
    if (!is_string(argv[0])) {
        vm_error(vm, "open: path must be a string");
        return VALUE_NIL;
    }
    if (object_type(argv[1]) != TYPE_KEYWORD) {
        vm_error(vm, "open: mode must be a keyword (:read :write :append)");
        return VALUE_NIL;
    }
    const char* path = string_cstr(argv[0]);
    const char* mode_name = keyword_name(argv[1]);
    const char* mode;
    if (strcmp(mode_name, "read") == 0) mode = "r";
    else if (strcmp(mode_name, "write") == 0) mode = "w";
    else if (strcmp(mode_name, "append") == 0) mode = "a";
    else {
        vm_error(vm, "open: mode must be :read, :write, or :append");
        return VALUE_NIL;
    }
    Value result = stream_open(path, mode);
    if (is_nil(result)) {
        vm_error(vm, "open: failed to open file");
        return VALUE_NIL;
    }
    return result;
}

/* close: (close stream) */
static Value native_close(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "close: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "close: argument must be a stream");
        return VALUE_NIL;
    }
    stream_close(argv[0]);
    return VALUE_NIL;
}

/* read-line: (read-line) or (read-line stream) */
static Value native_read_line(VM* vm, int argc, Value* argv) {
    if (argc > 1) {
        vm_error(vm, "read-line: requires 0 or 1 arguments");
        return VALUE_NIL;
    }
    Value s;
    if (argc == 0) {
        /* Read from *in* — look up the var */
        Namespace* ns = namespace_registry_current(global_namespace_registry);
        Value in_sym = symbol_intern("*in*");
        Var* var = namespace_lookup(ns, in_sym);
        if (!var) {
            vm_error(vm, "read-line: *in* not defined");
            return VALUE_NIL;
        }
        s = var_get_value(var);
    } else {
        s = argv[0];
    }
    if (!is_stream(s)) {
        vm_error(vm, "read-line: argument must be a stream");
        return VALUE_NIL;
    }

    /* Non-blocking path when running inside a scheduled task */
    Stream* st = (Stream*)untag_pointer(s);
    if (st->nonblocking && vm->scheduler && vm->scheduler->current) {
        bool would_block = false;
        Value result = stream_read_line_nb(s, &would_block);
        if (would_block) {
            Task* task = vm->scheduler->current;
            /* Guard: another task is already blocked on this stream */
            if (st->blocked_task && st->blocked_task != task) {
                vm_error(vm, "read-line: stream is already in use by another task");
                return VALUE_NIL;
            }
            /* Register fd with reactor, block task */
            st->blocked_task = task;
            io_reactor_register(vm->scheduler->io_reactor,
                                st->fd, true, false, task);
            scheduler_block_io(vm->scheduler, task);
            vm->native_blocked = true;
            vm->yielded = true;
            return VALUE_NIL;  /* ignored — CALL will rewind */
        }
        /* Clear blocked_task on successful read (retry succeeded) */
        st->blocked_task = NULL;
        return result;
    }

    /* Blocking fallback (standalone VM or stdio) */
    if (st->nonblocking) {
        /* Temporarily clear O_NONBLOCK for blocking read */
        int fl = fcntl(st->fd, F_GETFL);
        fcntl(st->fd, F_SETFL, fl & ~O_NONBLOCK);
        Value result = stream_read_line(s);
        fcntl(st->fd, F_SETFL, fl);
        return result;
    }
    return stream_read_line(s);
}

/* write: (write stream s) */
static Value native_write_stream(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "write: requires exactly 2 arguments");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "write: first argument must be a stream");
        return VALUE_NIL;
    }
    if (!is_string(argv[1])) {
        vm_error(vm, "write: second argument must be a string");
        return VALUE_NIL;
    }
    stream_write_string(argv[0], string_cstr(argv[1]), string_byte_length(argv[1]));
    return VALUE_NIL;
}

/* flush: (flush stream) */
static Value native_flush(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "flush: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "flush: argument must be a stream");
        return VALUE_NIL;
    }
    stream_flush(argv[0]);
    return VALUE_NIL;
}

/* slurp: (slurp path) */
static Value native_slurp(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "slurp: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_string(argv[0])) {
        vm_error(vm, "slurp: argument must be a string");
        return VALUE_NIL;
    }
    Value result = stream_slurp(string_cstr(argv[0]));
    if (is_nil(result)) {
        vm_error(vm, "slurp: failed to read file");
        return VALUE_NIL;
    }
    return result;
}

/* spit: (spit path content) or (spit path content :append true) */
static Value native_spit(VM* vm, int argc, Value* argv) {
    if (argc < 2 || argc > 4) {
        vm_error(vm, "spit: requires 2 to 4 arguments");
        return VALUE_NIL;
    }
    if (!is_string(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "spit: path and content must be strings");
        return VALUE_NIL;
    }
    bool append = false;
    if (argc == 4) {
        /* Check for :append true */
        if (is_pointer(argv[2]) && object_type(argv[2]) == TYPE_KEYWORD &&
            strcmp(keyword_name(argv[2]), "append") == 0 &&
            is_true(argv[3])) {
            append = true;
        }
    }
    int ret = stream_spit(string_cstr(argv[0]),
                          string_cstr(argv[1]),
                          string_byte_length(argv[1]),
                          append);
    if (ret != 0) {
        vm_error(vm, "spit: failed to write file");
        return VALUE_NIL;
    }
    return VALUE_NIL;
}

/* stream?: (stream? x) */
static Value native_stream_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "stream?: requires exactly 1 argument");
        return VALUE_NIL;
    }
    return is_stream(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

void core_register_streams(void) {
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) return;

    register_native(core_ns, "open", native_open);
    register_native(core_ns, "close", native_close);
    register_native(core_ns, "read-line", native_read_line);
    register_native(core_ns, "write", native_write_stream);
    register_native(core_ns, "flush", native_flush);
    register_native(core_ns, "slurp", native_slurp);
    register_native(core_ns, "spit", native_spit);
    register_native(core_ns, "stream?", native_stream_q);
}

/* =================================================================
 * Concurrency: Channels and Task predicates
 * ================================================================= */

/* chan: (chan) or (chan n) — create unbuffered or buffered channel */
static Value native_chan(VM* vm, int argc, Value* argv) {
    if (argc > 1) {
        vm_error(vm, "chan: requires 0 or 1 arguments");
        return VALUE_NIL;
    }
    int capacity = 0;
    if (argc == 1) {
        if (!is_fixnum(argv[0])) {
            vm_error(vm, "chan: capacity must be an integer");
            return VALUE_NIL;
        }
        capacity = (int)untag_fixnum(argv[0]);
        if (capacity < 0) {
            vm_error(vm, "chan: capacity must be non-negative");
            return VALUE_NIL;
        }
    }
    return channel_new(capacity);
}

/* >! and <! are now special forms (OP_CHAN_SEND/OP_CHAN_RECV) */

/* close!: (close! ch) — close channel */
static Value native_chan_close(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "close!: requires 1 argument (channel)");
        return VALUE_NIL;
    }
    if (!is_channel(argv[0])) {
        vm_error(vm, "close!: argument must be a channel");
        return VALUE_NIL;
    }
    Scheduler* sched = vm->scheduler ? vm->scheduler : global_scheduler;
    if (sched) {
        channel_close(argv[0], sched);
    } else {
        Channel* ch = channel_get(argv[0]);
        ch->closed = true;
    }
    return VALUE_NIL;
}

/* channel?: (channel? x) */
static Value native_channel_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "channel?: requires 1 argument");
        return VALUE_NIL;
    }
    return is_channel(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

/* task?: (task? x) */
static Value native_task_q(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "task?: requires 1 argument");
        return VALUE_NIL;
    }
    return is_task(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

/* =================================================================
 * beer.tar namespace — tar archive operations
 * ================================================================= */

/* (tar/list path) => vector of maps [{:name "foo" :size 123 :offset 512} ...] */
static Value native_tar_list(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "tar/list: requires 1 string argument (path)");
        return VALUE_NIL;
    }
    TarEntry* entries = NULL;
    int count = tar_list_entries(string_cstr(argv[0]), &entries);
    Value result = vector_create((size_t)(count > 0 ? count : 0));
    for (int i = 0; i < count; i++) {
        Value m = hashmap_create_default();
        Value k_name = keyword_intern("name");
        Value k_size = keyword_intern("size");
        Value k_offset = keyword_intern("offset");
        Value v_name = string_from_cstr(entries[i].ns_path);
        Value v_size = make_fixnum((int64_t)entries[i].file_size);
        Value v_offset = make_fixnum((int64_t)entries[i].data_offset);
        hashmap_set(m, k_name, v_name);
        hashmap_set(m, k_size, v_size);
        hashmap_set(m, k_offset, v_offset);
        object_release(v_name);
        vector_push(result, m);
        object_release(m);
    }
    free(entries);
    return result;
}

/* (tar/read-entry path entry-name) => string contents */
static Value native_tar_read_entry(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_string(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "tar/read-entry: requires 2 string arguments (path name)");
        return VALUE_NIL;
    }
    char* contents = tar_read_file(string_cstr(argv[0]), string_cstr(argv[1]));
    if (!contents) {
        vm_error(vm, "tar/read-entry: entry not found in tar");
        return VALUE_NIL;
    }
    Value result = string_from_cstr(contents);
    free(contents);
    return result;
}

/* (tar/create path file-map) => nil, creates tar file
   file-map: {"rel/path" "contents" ...} */
static Value native_tar_create(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_string(argv[0])) {
        vm_error(vm, "tar/create: requires path (string) and file-map (map)");
        return VALUE_NIL;
    }
    if (!is_pointer(argv[1]) || object_type(argv[1]) != TYPE_HASHMAP) {
        vm_error(vm, "tar/create: second argument must be a map");
        return VALUE_NIL;
    }
    Value keys = hashmap_keys(argv[1]);
    size_t n = vector_length(keys);
    const char** names = malloc(n * sizeof(char*));
    const char** contents = malloc(n * sizeof(char*));
    /* We need to hold string Values alive */
    Value* val_refs = malloc(n * sizeof(Value));

    for (size_t i = 0; i < n; i++) {
        Value k = vector_get(keys, i);
        Value v = hashmap_get(argv[1], k);
        if (!is_string(k) || !is_string(v)) {
            free(names); free(contents); free(val_refs);
            object_release(keys);
            vm_error(vm, "tar/create: all keys and values must be strings");
            return VALUE_NIL;
        }
        names[i] = string_cstr(k);
        contents[i] = string_cstr(v);
        val_refs[i] = v;
    }

    int rc = tar_create(string_cstr(argv[0]), names, contents, (int)n);
    free(names);
    free(contents);
    free(val_refs);
    object_release(keys);

    if (rc != 0) {
        vm_error(vm, "tar/create: failed to create tar file");
        return VALUE_NIL;
    }
    return VALUE_NIL;
}

void core_register_tar(void) {
    Namespace* tar_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.tar");
    if (!tar_ns) return;
    register_native(tar_ns, "tar-list", native_tar_list);
    register_native(tar_ns, "tar-read-entry", native_tar_read_entry);
    register_native(tar_ns, "tar-create", native_tar_create);
}

void core_register_concurrency(void) {
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) return;

    register_native(core_ns, "chan", native_chan);
    /* >! and <! are now special forms (OP_CHAN_SEND/OP_CHAN_RECV) */
    register_native(core_ns, "close!", native_chan_close);
    register_native(core_ns, "channel?", native_channel_q);
    register_native(core_ns, "task?", native_task_q);
}

/* =================================================================
 * macroexpand-1 / macroexpand
 * ================================================================= */

/* Helper: if form is (macro-name args...), expand once; else return form unchanged.
 * Returns an OWNED reference (caller must release if heap object). */
static Value do_macroexpand_1(VM* vm, Value form) {
    if (!is_cons(form)) goto return_form;

    Value head = car(form);
    if (!is_pointer(head) || object_type(head) != TYPE_SYMBOL) goto return_form;

    if (!global_namespace_registry) goto return_form;
    Namespace* ns = namespace_registry_current(global_namespace_registry);
    if (!ns) goto return_form;

    Var* var = namespace_lookup(ns, head);
    if (!var) {
        Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
        if (core_ns && core_ns != ns) {
            var = namespace_lookup(core_ns, head);
        }
    }
    if (!var || !var->is_macro) goto return_form;

    Value macro_fn = var_get_value(var);

    /* Collect unevaluated arg forms */
    Value arg_forms = cdr(form);
    int n_args = 0;
    Value cur = arg_forms;
    while (is_cons(cur)) { n_args++; cur = cdr(cur); }

    /* Build mini constant pool: [arg0, arg1, ..., macro_fn] */
    int n_consts = n_args + 1;
    Value* consts = malloc(sizeof(Value) * (size_t)n_consts);
    cur = arg_forms;
    for (int i = 0; i < n_args; i++) {
        consts[i] = car(cur);
        cur = cdr(cur);
    }
    consts[n_args] = macro_fn;

    /* Build mini bytecode */
    size_t code_size = (size_t)(n_consts * 5 + 3 + 1);
    uint8_t* code = malloc(code_size);
    size_t pc = 0;

    for (int i = 0; i < n_args; i++) {
        code[pc++] = OP_PUSH_CONST;
        code[pc++] = (uint8_t)(i & 0xFF);
        code[pc++] = (uint8_t)((i >> 8) & 0xFF);
        code[pc++] = (uint8_t)((i >> 16) & 0xFF);
        code[pc++] = (uint8_t)((i >> 24) & 0xFF);
    }
    code[pc++] = OP_PUSH_CONST;
    code[pc++] = (uint8_t)(n_args & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 8) & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 16) & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 24) & 0xFF);
    code[pc++] = OP_CALL;
    code[pc++] = (uint8_t)(n_args & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 8) & 0xFF);
    code[pc++] = OP_HALT;

    VM* tmp_vm = vm_new(256);
    vm_load_code(tmp_vm, code, (int)pc);
    vm_load_constants(tmp_vm, consts, n_consts);
    vm_run(tmp_vm);

    Value result = VALUE_NIL;
    if (tmp_vm->error) {
        char buf[256];
        snprintf(buf, sizeof(buf), "macroexpand failed: %s", tmp_vm->error_msg);
        vm_error(vm, buf);
    } else if (tmp_vm->stack_pointer > 0) {
        result = tmp_vm->stack[tmp_vm->stack_pointer - 1];
        if (is_pointer(result)) object_retain(result);
    }

    vm_free(tmp_vm);
    free(code);
    free(consts);
    return result;

return_form:
    if (is_pointer(form)) object_retain(form);
    return form;
}

static Value native_macroexpand_1(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "macroexpand-1: requires exactly 1 argument");
        return VALUE_NIL;
    }
    return do_macroexpand_1(vm, argv[0]);
}

static Value native_macroexpand(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "macroexpand: requires exactly 1 argument");
        return VALUE_NIL;
    }
    Value form = argv[0];
    if (is_pointer(form)) object_retain(form);  /* own our working copy */
    for (;;) {
        Value expanded = do_macroexpand_1(vm, form);  /* returns owned ref */
        if (vm->error) {
            if (is_pointer(form)) object_release(form);
            return VALUE_NIL;
        }
        if (value_equal(expanded, form)) {
            if (is_pointer(expanded)) object_release(expanded);
            return form;  /* already owned */
        }
        if (is_pointer(form)) object_release(form);
        form = expanded;
    }
}

/* =================================================================
 * load: Read, compile, and execute all forms from a file
 * ================================================================= */

/* Kept-alive compiled units (same pattern as REPL) */
#define MAX_LOAD_UNITS 256
static CompiledCode* load_units[MAX_LOAD_UNITS];
static Value* load_constants[MAX_LOAD_UNITS];
static int n_load_units = 0;

/* Load and execute beerlang source from a buffer (used by tar require) */
static Value load_from_buffer(VM* caller_vm, const char* source, const char* name) {
    (void)caller_vm;
    Reader* reader = reader_new(source, name);
    Value forms = reader_read_all(reader);

    if (reader_has_error(reader)) {
        fprintf(stderr, "load: read error in %s: %s\n", name, reader_error_msg(reader));
        reader_free(reader);
        return VALUE_NIL;
    }
    reader_free(reader);

    size_t n_forms = vector_length(forms);
    Value last_result = VALUE_NIL;

    for (size_t i = 0; i < n_forms; i++) {
        Value form = vector_get(forms, i);

        Compiler* compiler = compiler_new(name);
        CompiledCode* code = compile(compiler, form);

        if (compiler_has_error(compiler)) {
            fprintf(stderr, "load: compile error in %s (form %zu): %s\n", name, i, compiler_error_msg(compiler));
            compiled_code_free(code);
            compiler_free(compiler);
            object_release(forms);
            return VALUE_NIL;
        }
        compiler_free(compiler);

        int n_constants = (int)vector_length(code->constants);
        Value* constants = malloc((size_t)n_constants * sizeof(Value));
        for (int j = 0; j < n_constants; j++) {
            constants[j] = vector_get(code->constants, (size_t)j);
        }

        Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
        const char* cur_ns_name = cur_ns ? cur_ns->name : NULL;
        for (int j = 0; j < n_constants; j++) {
            if (is_function(constants[j])) {
                function_set_code(constants[j],
                                  code->bytecode, (int)code->code_size,
                                  constants, n_constants);
                function_set_ns_name(constants[j], cur_ns_name);
            }
        }

        VM* vm = vm_new(256);
        vm->scheduler = global_scheduler;
        vm_load_code(vm, code->bytecode, (int)code->code_size);
        vm_load_constants(vm, constants, n_constants);
        vm_run(vm);

        if (vm->error) {
            fprintf(stderr, "load: runtime error in %s: %s\n", name, vm->error_msg);
            vm_free(vm);
            compiled_code_free(code);
            free(constants);
            object_release(forms);
            return VALUE_NIL;
        }

        if (is_pointer(last_result)) object_release(last_result);
        last_result = VALUE_NIL;
        if (!vm_stack_empty(vm)) {
            last_result = vm->stack[vm->stack_pointer - 1];
            if (is_pointer(last_result)) object_retain(last_result);
        }

        vm_free(vm);

        if (n_load_units < MAX_LOAD_UNITS) {
            load_units[n_load_units] = code;
            load_constants[n_load_units] = constants;
            n_load_units++;
        } else {
            fprintf(stderr, "load: warning: too many compiled units\n");
            compiled_code_free(code);
            free(constants);
        }
    }

    object_release(forms);
    return last_result;
}

Value native_load(VM* caller_vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(caller_vm, "load: requires exactly 1 argument (filename)");
        return VALUE_NIL;
    }
    Value path = argv[0];
    if (!is_pointer(path) || object_type(path) != TYPE_STRING) {
        vm_error(caller_vm, "load: argument must be a string");
        return VALUE_NIL;
    }

    const char* filename = string_cstr(path);

    /* Read file into memory */
    FILE* f = fopen(filename, "r");
    if (!f) {
        vm_error(caller_vm, "load: cannot open file");
        return VALUE_NIL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = malloc((size_t)fsize + 1);
    if (fsize > 0) {
        size_t read_n = fread(source, 1, (size_t)fsize, f);
        source[read_n] = '\0';
    } else {
        source[0] = '\0';
    }
    fclose(f);

    /* Read all forms */
    Reader* reader = reader_new(source, filename);
    Value forms = reader_read_all(reader);

    if (reader_has_error(reader)) {
        fprintf(stderr, "load: read error in %s: %s\n", filename, reader_error_msg(reader));
        reader_free(reader);
        free(source);
        return VALUE_NIL;
    }
    reader_free(reader);
    free(source);

    /* Compile and execute each form */
    size_t n_forms = vector_length(forms);
    Value last_result = VALUE_NIL;

    for (size_t i = 0; i < n_forms; i++) {
        Value form = vector_get(forms, i);

        /* Compile */
        Compiler* compiler = compiler_new(filename);
        CompiledCode* code = compile(compiler, form);

        if (compiler_has_error(compiler)) {
            fprintf(stderr, "load: compile error in %s (form %zu): %s\n", filename, i, compiler_error_msg(compiler));
            compiled_code_free(code);
            compiler_free(compiler);
            object_release(forms);
            return VALUE_NIL;
        }
        compiler_free(compiler);

        /* Build constants array */
        int n_constants = (int)vector_length(code->constants);
        Value* constants = malloc((size_t)n_constants * sizeof(Value));
        for (int j = 0; j < n_constants; j++) {
            constants[j] = vector_get(code->constants, (size_t)j);
        }

        /* Set execution context on function objects */
        Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
        const char* cur_ns_name = cur_ns ? cur_ns->name : NULL;
        for (int j = 0; j < n_constants; j++) {
            if (is_function(constants[j])) {
                function_set_code(constants[j],
                                  code->bytecode, (int)code->code_size,
                                  constants, n_constants);
                function_set_ns_name(constants[j], cur_ns_name);
            }
        }

        /* Execute */
        VM* vm = vm_new(256);
        vm->scheduler = global_scheduler;
        vm_load_code(vm, code->bytecode, (int)code->code_size);
        vm_load_constants(vm, constants, n_constants);
        vm_run(vm);

        if (vm->error) {
            fprintf(stderr, "load: runtime error in %s: %s\n", filename, vm->error_msg);
            vm_free(vm);
            compiled_code_free(code);
            free(constants);
            object_release(forms);
            return VALUE_NIL;
        }

        /* Capture last result — retain before vm_free releases the stack */
        if (is_pointer(last_result)) object_release(last_result);
        last_result = VALUE_NIL;
        if (!vm_stack_empty(vm)) {
            last_result = vm->stack[vm->stack_pointer - 1];
            if (is_pointer(last_result)) object_retain(last_result);
        }

        vm_free(vm);

        /* Keep compiled code alive (functions reference it) */
        if (n_load_units < MAX_LOAD_UNITS) {
            load_units[n_load_units] = code;
            load_constants[n_load_units] = constants;
            n_load_units++;
        } else {
            fprintf(stderr, "load: warning: too many compiled units\n");
            compiled_code_free(code);
            free(constants);
        }
    }

    object_release(forms);
    return last_result;
}

/* ns-publics: (ns-publics 'some-ns) => list of symbols defined in namespace */
Value native_ns_publics(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "ns-publics: requires exactly 1 argument (a symbol)");
        return VALUE_NIL;
    }
    if (!(is_pointer(argv[0]) && object_type(argv[0]) == TYPE_SYMBOL)) {
        vm_error(vm, "ns-publics: argument must be a symbol");
        return VALUE_NIL;
    }
    const char* ns_name = symbol_name(argv[0]);
    Namespace* ns = namespace_registry_get(global_namespace_registry, ns_name);
    if (!ns) {
        char buf[256];
        snprintf(buf, sizeof(buf), "ns-publics: namespace '%s' not found", ns_name);
        vm_error(vm, buf);
        return VALUE_NIL;
    }
    Value keys_vec = hashmap_keys(ns->vars);
    Value result = vector_to_list(keys_vec);
    object_release(keys_vec);
    return result;
}

/* =================================================================
 * Core Macros Registration (load lib/core.beer)
 * ================================================================= */

void core_register_macros(void) {
    /* Build candidate paths for core.beer */
    char env_path[1024] = {0};
    const char* beerpath = getenv("BEERPATH");
    const char* beer_lib = getenv("BEER_LIB_PATH");
    if (beerpath) {
        /* Use first entry from BEERPATH */
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", beerpath);
        char* colon = strchr(tmp, ':');
        if (colon) *colon = '\0';
        if (tmp[0] != '\0')
            snprintf(env_path, sizeof(env_path), "%s/core.beer", tmp);
    } else if (beer_lib) {
        snprintf(env_path, sizeof(env_path), "%s/core.beer", beer_lib);
    }

    const char* paths[4];
    int n = 0;
    if (env_path[0]) paths[n++] = env_path;
    paths[n++] = "lib/core.beer";       /* running from project root */
    paths[n++] = "../lib/core.beer";    /* running from bin/ */
    paths[n] = NULL;

    for (int i = 0; paths[i]; i++) {
        FILE* f = fopen(paths[i], "r");
        if (f) {
            fclose(f);

            /* Temporarily switch to beer.core so defs go there */
            Namespace* saved_ns = namespace_registry_current(global_namespace_registry);
            Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
            namespace_registry_set_current(global_namespace_registry, core_ns);

            /* Use the load machinery directly */
            Value path_str = string_from_cstr(paths[i]);
            Value argv[1] = { path_str };
            VM* vm = vm_new(16);  /* dummy VM for error reporting */
            native_load(vm, 1, argv);
            if (vm->error) {
                fprintf(stderr, "WARNING: Failed to load core macros: %s\n", vm->error_msg);
            }
            vm_free(vm);
            object_release(path_str);

            /* Restore original namespace */
            namespace_registry_set_current(global_namespace_registry, saved_ns);
            return;
        }
    }

    /* Not found - not fatal, just no core macros */
    fprintf(stderr, "NOTE: lib/core.beer not found, core macros not loaded\n");
}
