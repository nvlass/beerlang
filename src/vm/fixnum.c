/* Fixnum arithmetic implementation
 *
 * Implements checked arithmetic for tagged fixnums with overflow detection.
 */

#include <stdint.h>
#include <limits.h>
#include "fixnum.h"
#include "beerlang.h"

/* Fixnum range: full int64 (tagged union stores raw int64, no bit shifting)
 * Min: INT64_MIN
 * Max: INT64_MAX
 */
#define FIXNUM_MIN INT64_MIN
#define FIXNUM_MAX INT64_MAX

/* Checked addition */
bool fixnum_add_checked(Value a, Value b, Value* result) {
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);
    int64_t sum;

    /* Check for overflow using compiler builtins if available */
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_add_overflow(ia, ib, &sum)) {
        return true;  /* Overflow occurred */
    }
#else
    /* Manual overflow detection */
    sum = ia + ib;

    /* Overflow if both operands have same sign but result has different sign */
    if (((ia ^ sum) & (ib ^ sum)) < 0) {
        return true;  /* Overflow */
    }
#endif

    /* Also check if result fits in fixnum range (61 bits) */
    if (sum < FIXNUM_MIN || sum > FIXNUM_MAX) {
        return true;  /* Out of fixnum range */
    }

    *result = make_fixnum(sum);
    return false;
}

/* Checked subtraction */
bool fixnum_sub_checked(Value a, Value b, Value* result) {
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);
    int64_t diff;

#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_sub_overflow(ia, ib, &diff)) {
        return true;  /* Overflow occurred */
    }
#else
    /* Manual overflow detection */
    diff = ia - ib;

    /* Overflow if operands have different signs and result has different sign from a */
    if (((ia ^ ib) & (ia ^ diff)) < 0) {
        return true;  /* Overflow */
    }
#endif

    /* Also check if result fits in fixnum range (61 bits) */
    if (diff < FIXNUM_MIN || diff > FIXNUM_MAX) {
        return true;  /* Out of fixnum range */
    }

    *result = make_fixnum(diff);
    return false;
}

/* Checked multiplication */
bool fixnum_mul_checked(Value a, Value b, Value* result) {
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);
    int64_t product;

    /* Special cases */
    if (ia == 0 || ib == 0) {
        *result = make_fixnum(0);
        return false;
    }

#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_mul_overflow(ia, ib, &product)) {
        return true;  /* Overflow occurred */
    }
#else
    /* Manual overflow detection */
    if (ia == FIXNUM_MIN || ib == FIXNUM_MIN) {
        /* Most negative value can only be multiplied by 0, 1, or -1 without overflow */
        if ((ia == FIXNUM_MIN && (ib < -1 || ib > 1)) ||
            (ib == FIXNUM_MIN && (ia < -1 || ia > 1))) {
            return true;  /* Overflow */
        }
    }

    product = ia * ib;

    /* Check if division gives back original value */
    if (product / ia != ib) {
        return true;  /* Overflow */
    }
#endif

    /* Also check if result fits in fixnum range (61 bits) */
    if (product < FIXNUM_MIN || product > FIXNUM_MAX) {
        return true;  /* Out of fixnum range */
    }

    *result = make_fixnum(product);
    return false;
}

/* Division - returns true if error (division by zero or overflow) */
bool fixnum_div(Value a, Value b, Value* quotient) {
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);

    /* Check for division by zero */
    if (ib == 0) {
        return true;  /* Error: division by zero */
    }

    /* Check for overflow: FIXNUM_MIN / -1 = overflow */
    if (ia == FIXNUM_MIN && ib == -1) {
        return true;  /* Error: overflow */
    }

    *quotient = make_fixnum(ia / ib);
    return false;
}

/* Modulo - returns true if error (division by zero) */
bool fixnum_mod(Value a, Value b, Value* remainder) {
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);

    /* Check for division by zero */
    if (ib == 0) {
        return true;  /* Error: division by zero */
    }

    *remainder = make_fixnum(ia % ib);
    return false;
}

/* Division with remainder */
bool fixnum_divmod(Value a, Value b, Value* quotient, Value* remainder) {
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);

    /* Check for division by zero */
    if (ib == 0) {
        return true;  /* Error: division by zero */
    }

    /* Check for overflow: FIXNUM_MIN / -1 = overflow */
    if (ia == FIXNUM_MIN && ib == -1) {
        return true;  /* Error: overflow */
    }

    *quotient = make_fixnum(ia / ib);
    *remainder = make_fixnum(ia % ib);
    return false;
}

/* Comparison - returns -1, 0, or 1 */
int fixnum_cmp(Value a, Value b) {
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);

    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

/* Bitwise AND */
Value fixnum_and(Value a, Value b) {
    return make_fixnum(untag_fixnum(a) & untag_fixnum(b));
}

/* Bitwise OR */
Value fixnum_or(Value a, Value b) {
    /* Need to preserve the tag */
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);
    return make_fixnum(ia | ib);
}

/* Bitwise XOR */
Value fixnum_xor(Value a, Value b) {
    /* XOR the numeric parts, preserve tag */
    int64_t ia = untag_fixnum(a);
    int64_t ib = untag_fixnum(b);
    return make_fixnum(ia ^ ib);
}

/* Bitwise NOT */
Value fixnum_not(Value a) {
    int64_t ia = untag_fixnum(a);
    return make_fixnum(~ia);
}

/* Left shift */
Value fixnum_shl(Value a, int shift) {
    /* TODO: Add overflow detection for shifts */
    int64_t ia = untag_fixnum(a);
    return make_fixnum(ia << shift);
}

/* Right shift (arithmetic) */
Value fixnum_shr(Value a, int shift) {
    int64_t ia = untag_fixnum(a);
    return make_fixnum(ia >> shift);
}

/* Negation with overflow check */
bool fixnum_neg_checked(Value a, Value* result) {
    int64_t ia = untag_fixnum(a);

    /* Check for overflow: -FIXNUM_MIN overflows */
    if (ia == FIXNUM_MIN) {
        return true;  /* Overflow */
    }

    *result = make_fixnum(-ia);
    return false;
}

/* Absolute value with overflow check */
bool fixnum_abs_checked(Value a, Value* result) {
    int64_t ia = untag_fixnum(a);

    /* Check for overflow: abs(FIXNUM_MIN) overflows */
    if (ia == FIXNUM_MIN) {
        return true;  /* Overflow */
    }

    *result = make_fixnum(ia < 0 ? -ia : ia);
    return false;
}

/* Utility predicates */
bool fixnum_is_zero(Value v) {
    return untag_fixnum(v) == 0;
}

bool fixnum_is_positive(Value v) {
    return untag_fixnum(v) > 0;
}

bool fixnum_is_negative(Value v) {
    return untag_fixnum(v) < 0;
}

bool fixnum_is_even(Value v) {
    return (untag_fixnum(v) & 1) == 0;
}

bool fixnum_is_odd(Value v) {
    return (untag_fixnum(v) & 1) == 1;
}
