/* Bigint - Arbitrary precision integers using mini-gmp
 *
 * Bigints are heap-allocated objects that use mini-gmp for storage.
 * They are automatically used when fixnum operations overflow.
 */

#ifndef BEERLANG_BIGINT_H
#define BEERLANG_BIGINT_H

#include <stdbool.h>
#include "value.h"

/* Bigint object structure (heap-allocated)
 * Layout:
 *   Object header (16 bytes)
 *   mpz_t data (variable size, managed by mini-gmp)
 */

/* Create a bigint from int64_t */
Value bigint_from_int64(int64_t n);

/* Create a bigint from string (decimal by default)
 * Returns VALUE_NIL on parse error
 */
Value bigint_from_string(const char* str, int base);

/* Promote a fixnum to bigint */
Value bigint_from_fixnum(Value fixnum);

/* Convert bigint to int64_t if it fits, returns true on success */
bool bigint_to_int64(Value bigint, int64_t* out);

/* Try to convert bigint to fixnum if it fits in range */
Value bigint_try_demote(Value bigint);

/* Arithmetic operations - return new bigint */
Value bigint_add(Value a, Value b);
Value bigint_sub(Value a, Value b);
Value bigint_mul(Value a, Value b);
Value bigint_div(Value a, Value b);  /* Division (quotient) */
Value bigint_mod(Value a, Value b);  /* Modulo (remainder) */
Value bigint_divmod(Value a, Value b, Value* remainder);  /* Both */

/* Comparison - returns -1, 0, or 1 */
int bigint_cmp(Value a, Value b);

static inline bool bigint_eq(Value a, Value b) {
    return bigint_cmp(a, b) == 0;
}

static inline bool bigint_lt(Value a, Value b) {
    return bigint_cmp(a, b) < 0;
}

static inline bool bigint_le(Value a, Value b) {
    return bigint_cmp(a, b) <= 0;
}

static inline bool bigint_gt(Value a, Value b) {
    return bigint_cmp(a, b) > 0;
}

static inline bool bigint_ge(Value a, Value b) {
    return bigint_cmp(a, b) >= 0;
}

/* Bitwise operations */
Value bigint_and(Value a, Value b);
Value bigint_or(Value a, Value b);
Value bigint_xor(Value a, Value b);
Value bigint_not(Value a);

/* Unary operations */
Value bigint_neg(Value a);
Value bigint_abs(Value a);

/* Predicates */
bool bigint_is_zero(Value v);
bool bigint_is_positive(Value v);
bool bigint_is_negative(Value v);
bool bigint_is_even(Value v);
bool bigint_is_odd(Value v);

/* Convert bigint to string (caller must free the returned string) */
char* bigint_to_string(Value bigint, int base);

/* Convert bigint to double */
double bigint_to_double(Value bigint);

/* Print bigint (for debugging) */
void bigint_print(Value bigint);

/* Type check */
bool is_bigint(Value v);

#endif /* BEERLANG_BIGINT_H */
