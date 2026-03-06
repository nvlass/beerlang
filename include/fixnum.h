/* Fixnum arithmetic operations
 *
 * Provides checked arithmetic for tagged fixnums with overflow detection.
 * When overflow occurs, operations can promote to bigints.
 *
 * NOTE: Fixnums are immediate values (stored in the tagged Value itself),
 * not heap-allocated objects. They require no memory management, no
 * allocation, and no garbage collection.
 *
 * FUTURE OPTIMIZATION: Consider implementing the flyweight pattern to cache
 * commonly-used small fixnums (e.g., -128 to 255) for better performance.
 * See TODO_OPTIMIZATIONS.md for details.
 */

#ifndef BEERLANG_FIXNUM_H
#define BEERLANG_FIXNUM_H

#include <stdbool.h>
#include "value.h"

/* Checked arithmetic - returns true if overflow occurred */
bool fixnum_add_checked(Value a, Value b, Value* result);
bool fixnum_sub_checked(Value a, Value b, Value* result);
bool fixnum_mul_checked(Value a, Value b, Value* result);

/* Division operations - returns true if error (division by zero) */
bool fixnum_div(Value a, Value b, Value* quotient);
bool fixnum_mod(Value a, Value b, Value* remainder);
bool fixnum_divmod(Value a, Value b, Value* quotient, Value* remainder);

/* Comparison operations */
int fixnum_cmp(Value a, Value b);  /* Returns -1, 0, or 1 */

static inline bool fixnum_eq(Value a, Value b) {
    return a.as.fixnum == b.as.fixnum;
}

static inline bool fixnum_lt(Value a, Value b) {
    return fixnum_cmp(a, b) < 0;
}

static inline bool fixnum_le(Value a, Value b) {
    return fixnum_cmp(a, b) <= 0;
}

static inline bool fixnum_gt(Value a, Value b) {
    return fixnum_cmp(a, b) > 0;
}

static inline bool fixnum_ge(Value a, Value b) {
    return fixnum_cmp(a, b) >= 0;
}

/* Bitwise operations (always succeed for fixnums) */
Value fixnum_and(Value a, Value b);
Value fixnum_or(Value a, Value b);
Value fixnum_xor(Value a, Value b);
Value fixnum_not(Value a);
Value fixnum_shl(Value a, int shift);  /* Left shift */
Value fixnum_shr(Value a, int shift);  /* Right shift (arithmetic) */

/* Negation - returns true if overflow (negating most negative fixnum) */
bool fixnum_neg_checked(Value a, Value* result);

/* Absolute value - returns true if overflow */
bool fixnum_abs_checked(Value a, Value* result);

/* Utility functions */
bool fixnum_is_zero(Value v);
bool fixnum_is_positive(Value v);
bool fixnum_is_negative(Value v);
bool fixnum_is_even(Value v);
bool fixnum_is_odd(Value v);

#endif /* BEERLANG_FIXNUM_H */
