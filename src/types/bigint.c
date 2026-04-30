/* Bigint implementation using mini-gmp */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bigint.h"
#include "beerlang.h"
#include "mini-gmp.h"

/* Bigint object layout:
 *   Object header
 *   mpz_t data (stored inline)
 */
typedef struct {
    struct Object header;
    mpz_t mpz;
} Bigint;

/* Destructor - called when refcount reaches zero */
static void bigint_destructor(struct Object* obj) {
    Bigint* big = (Bigint*)obj;
    mpz_clear(big->mpz);
}

/* Initialize bigint type (idempotent - safe to call multiple times) */
static void bigint_init_type(void) {
    object_register_destructor(TYPE_BIGINT, bigint_destructor);
}

/* Create a bigint from int64_t */
Value bigint_from_int64(int64_t n) {
    bigint_init_type();

    Bigint* big = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value obj = tag_pointer(big);

    mpz_init(big->mpz);
    mpz_set_si(big->mpz, n);

    return obj;
}

/* Create a bigint from string */
Value bigint_from_string(const char* str, int base) {
    bigint_init_type();

    Bigint* big = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value obj = tag_pointer(big);

    mpz_init(big->mpz);
    if (mpz_set_str(big->mpz, str, base) != 0) {
        /* Parse error */
        object_release(obj);
        return VALUE_NIL;
    }

    return obj;
}

/* Promote fixnum to bigint */
Value bigint_from_fixnum(Value fixnum) {
    assert(is_fixnum(fixnum));
    return bigint_from_int64(untag_fixnum(fixnum));
}

/* Convert bigint to int64_t if it fits */
bool bigint_to_int64(Value bigint, int64_t* out) {
    assert(is_pointer(bigint));
    assert(object_type(bigint) == TYPE_BIGINT);

    Bigint* big = (Bigint*)untag_pointer(bigint);

    /* Check if it fits in int64_t range */
    /* Compare with INT64_MAX and INT64_MIN */
    if (mpz_cmp_si(big->mpz, INT64_MAX) > 0 ||
        mpz_cmp_si(big->mpz, INT64_MIN) < 0) {
        return false;
    }

    *out = mpz_get_si(big->mpz);
    return true;
}

/* Try to demote bigint to fixnum */
Value bigint_try_demote(Value bigint) {
    assert(is_pointer(bigint));
    assert(object_type(bigint) == TYPE_BIGINT);

    int64_t n;
    if (bigint_to_int64(bigint, &n)) {
        /* Check if it fits in fixnum range (61 bits) */
        int64_t fixnum_min = INT64_MIN >> 3;
        int64_t fixnum_max = INT64_MAX >> 3;

        if (n >= fixnum_min && n <= fixnum_max) {
            return make_fixnum(n);
        }
    }

    /* Can't demote, return as-is */
    return bigint;
}

/* Addition */
Value bigint_add(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_add(big_r->mpz, big_a->mpz, big_b->mpz);

    return result;
}

/* Subtraction */
Value bigint_sub(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_sub(big_r->mpz, big_a->mpz, big_b->mpz);

    return result;
}

/* Multiplication */
Value bigint_mul(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_mul(big_r->mpz, big_a->mpz, big_b->mpz);

    return result;
}

/* Division (quotient) */
Value bigint_div(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    /* Check for division by zero */
    if (mpz_cmp_si(big_b->mpz, 0) == 0) {
        return VALUE_NIL;  /* Error: division by zero */
    }

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_tdiv_q(big_r->mpz, big_a->mpz, big_b->mpz);  /* Truncating division */

    return result;
}

/* Modulo (remainder) */
Value bigint_mod(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    /* Check for division by zero */
    if (mpz_cmp_si(big_b->mpz, 0) == 0) {
        return VALUE_NIL;  /* Error: division by zero */
    }

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_tdiv_r(big_r->mpz, big_a->mpz, big_b->mpz);  /* Truncating remainder */

    return result;
}

/* Division and modulo */
Value bigint_divmod(Value a, Value b, Value* remainder) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    /* Check for division by zero */
    if (mpz_cmp_si(big_b->mpz, 0) == 0) {
        *remainder = VALUE_NIL;
        return VALUE_NIL;  /* Error: division by zero */
    }

    Bigint* big_q = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value quotient = tag_pointer(big_q);
    mpz_init(big_q->mpz);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    *remainder = tag_pointer(big_r);
    mpz_init(big_r->mpz);

    mpz_tdiv_qr(big_q->mpz, big_r->mpz, big_a->mpz, big_b->mpz);

    return quotient;
}

/* Comparison */
int bigint_cmp(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    return mpz_cmp(big_a->mpz, big_b->mpz);
}

/* Bitwise AND */
Value bigint_and(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_and(big_r->mpz, big_a->mpz, big_b->mpz);

    return result;
}

/* Bitwise OR */
Value bigint_or(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_ior(big_r->mpz, big_a->mpz, big_b->mpz);

    return result;
}

/* Bitwise XOR */
Value bigint_xor(Value a, Value b) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);
    assert(is_pointer(b) && object_type(b) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);
    Bigint* big_b = (Bigint*)untag_pointer(b);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_xor(big_r->mpz, big_a->mpz, big_b->mpz);

    return result;
}

/* Bitwise NOT */
Value bigint_not(Value a) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_com(big_r->mpz, big_a->mpz);

    return result;
}

/* Negation */
Value bigint_neg(Value a) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_neg(big_r->mpz, big_a->mpz);

    return result;
}

/* Absolute value */
Value bigint_abs(Value a) {
    assert(is_pointer(a) && object_type(a) == TYPE_BIGINT);

    bigint_init_type();

    Bigint* big_a = (Bigint*)untag_pointer(a);

    Bigint* big_r = (Bigint*)object_alloc(TYPE_BIGINT, sizeof(Bigint));
    Value result = tag_pointer(big_r);

    mpz_init(big_r->mpz);
    mpz_abs(big_r->mpz, big_a->mpz);

    return result;
}

/* Predicates */
bool bigint_is_zero(Value v) {
    assert(is_pointer(v) && object_type(v) == TYPE_BIGINT);
    Bigint* big = (Bigint*)untag_pointer(v);
    return mpz_cmp_si(big->mpz, 0) == 0;
}

bool bigint_is_positive(Value v) {
    assert(is_pointer(v) && object_type(v) == TYPE_BIGINT);
    Bigint* big = (Bigint*)untag_pointer(v);
    return mpz_cmp_si(big->mpz, 0) > 0;
}

bool bigint_is_negative(Value v) {
    assert(is_pointer(v) && object_type(v) == TYPE_BIGINT);
    Bigint* big = (Bigint*)untag_pointer(v);
    return mpz_cmp_si(big->mpz, 0) < 0;
}

bool bigint_is_even(Value v) {
    assert(is_pointer(v) && object_type(v) == TYPE_BIGINT);
    Bigint* big = (Bigint*)untag_pointer(v);
    return mpz_even_p(big->mpz);
}

bool bigint_is_odd(Value v) {
    assert(is_pointer(v) && object_type(v) == TYPE_BIGINT);
    Bigint* big = (Bigint*)untag_pointer(v);
    return mpz_odd_p(big->mpz);
}

/* Convert to string */
char* bigint_to_string(Value bigint, int base) {
    assert(is_pointer(bigint) && object_type(bigint) == TYPE_BIGINT);
    Bigint* big = (Bigint*)untag_pointer(bigint);
    return mpz_get_str(NULL, base, big->mpz);
}

/* Convert bigint to double */
double bigint_to_double(Value bigint) {
    assert(is_pointer(bigint) && object_type(bigint) == TYPE_BIGINT);
    Bigint* big = (Bigint*)untag_pointer(bigint);
    return mpz_get_d(big->mpz);
}

/* Print bigint */
void bigint_print(Value bigint) {
    char* str = bigint_to_string(bigint, 10);
    printf("%s", str);
    free(str);
}

/* Type check */
bool is_bigint(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_BIGINT;
}
