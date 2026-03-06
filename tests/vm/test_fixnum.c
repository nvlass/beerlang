/* Test fixnum arithmetic operations */

#include <stdio.h>
#include <stdint.h>
#include "test.h"
#include "beerlang.h"
#include "fixnum.h"

/* Test basic addition */
TEST(fixnum_addition) {
    Value a = make_fixnum(10);
    Value b = make_fixnum(20);
    Value result;

    ASSERT(!fixnum_add_checked(a, b, &result), "Addition should not overflow");
    ASSERT_EQ(untag_fixnum(result), 30, "10 + 20 = 30");

    /* Negative numbers */
    a = make_fixnum(-15);
    b = make_fixnum(25);
    ASSERT(!fixnum_add_checked(a, b, &result), "Addition should not overflow");
    ASSERT_EQ(untag_fixnum(result), 10, "-15 + 25 = 10");

    /* Zero */
    a = make_fixnum(0);
    b = make_fixnum(42);
    ASSERT(!fixnum_add_checked(a, b, &result), "Addition should not overflow");
    ASSERT_EQ(untag_fixnum(result), 42, "0 + 42 = 42");

    return NULL;
}

/* Test addition overflow */
TEST(fixnum_addition_overflow) {
    /* INT64_MAX + 1 overflows */
    Value a = make_fixnum(INT64_MAX);
    Value b = make_fixnum(1);
    Value result;

    ASSERT(fixnum_add_checked(a, b, &result), "Should detect overflow");

    /* INT64_MIN + (-1) overflows */
    a = make_fixnum(INT64_MIN);
    b = make_fixnum(-1);
    ASSERT(fixnum_add_checked(a, b, &result), "Should detect negative overflow");

    return NULL;
}

/* Test subtraction */
TEST(fixnum_subtraction) {
    Value a = make_fixnum(50);
    Value b = make_fixnum(30);
    Value result;

    ASSERT(!fixnum_sub_checked(a, b, &result), "Subtraction should not overflow");
    ASSERT_EQ(untag_fixnum(result), 20, "50 - 30 = 20");

    /* Negative result */
    a = make_fixnum(10);
    b = make_fixnum(25);
    ASSERT(!fixnum_sub_checked(a, b, &result), "Subtraction should not overflow");
    ASSERT_EQ(untag_fixnum(result), -15, "10 - 25 = -15");

    return NULL;
}

/* Test subtraction overflow */
TEST(fixnum_subtraction_overflow) {
    /* INT64_MIN - 1 should overflow */
    Value a = make_fixnum(INT64_MIN);
    Value b = make_fixnum(1);
    Value result;

    ASSERT(fixnum_sub_checked(a, b, &result), "Should detect overflow");

    return NULL;
}

/* Test multiplication */
TEST(fixnum_multiplication) {
    Value a = make_fixnum(6);
    Value b = make_fixnum(7);
    Value result;

    ASSERT(!fixnum_mul_checked(a, b, &result), "Multiplication should not overflow");
    ASSERT_EQ(untag_fixnum(result), 42, "6 * 7 = 42");

    /* Negative */
    a = make_fixnum(-5);
    b = make_fixnum(8);
    ASSERT(!fixnum_mul_checked(a, b, &result), "Multiplication should not overflow");
    ASSERT_EQ(untag_fixnum(result), -40, "-5 * 8 = -40");

    /* Zero */
    a = make_fixnum(0);
    b = make_fixnum(12345);
    ASSERT(!fixnum_mul_checked(a, b, &result), "Multiplication should not overflow");
    ASSERT_EQ(untag_fixnum(result), 0, "0 * n = 0");

    return NULL;
}

/* Test multiplication overflow */
TEST(fixnum_multiplication_overflow) {
    /* Large numbers */
    Value a = make_fixnum(1LL << 40);
    Value b = make_fixnum(1LL << 40);
    Value result;

    ASSERT(fixnum_mul_checked(a, b, &result), "Should detect overflow");

    return NULL;
}

/* Test division */
TEST(fixnum_division) {
    Value a = make_fixnum(42);
    Value b = make_fixnum(6);
    Value quotient;

    ASSERT(!fixnum_div(a, b, &quotient), "Division should succeed");
    ASSERT_EQ(untag_fixnum(quotient), 7, "42 / 6 = 7");

    /* Negative */
    a = make_fixnum(-20);
    b = make_fixnum(4);
    ASSERT(!fixnum_div(a, b, &quotient), "Division should succeed");
    ASSERT_EQ(untag_fixnum(quotient), -5, "-20 / 4 = -5");

    /* Truncation */
    a = make_fixnum(10);
    b = make_fixnum(3);
    ASSERT(!fixnum_div(a, b, &quotient), "Division should succeed");
    ASSERT_EQ(untag_fixnum(quotient), 3, "10 / 3 = 3 (truncated)");

    return NULL;
}

/* Test division by zero */
TEST(fixnum_division_by_zero) {
    Value a = make_fixnum(42);
    Value b = make_fixnum(0);
    Value quotient;

    ASSERT(fixnum_div(a, b, &quotient), "Should detect division by zero");

    return NULL;
}

/* Test modulo */
TEST(fixnum_modulo) {
    Value a = make_fixnum(17);
    Value b = make_fixnum(5);
    Value remainder;

    ASSERT(!fixnum_mod(a, b, &remainder), "Modulo should succeed");
    ASSERT_EQ(untag_fixnum(remainder), 2, "17 % 5 = 2");

    /* Negative */
    a = make_fixnum(-17);
    b = make_fixnum(5);
    ASSERT(!fixnum_mod(a, b, &remainder), "Modulo should succeed");
    ASSERT_EQ(untag_fixnum(remainder), -2, "-17 % 5 = -2");

    return NULL;
}

/* Test comparison */
TEST(fixnum_comparison) {
    Value a = make_fixnum(10);
    Value b = make_fixnum(20);
    Value c = make_fixnum(10);

    ASSERT(fixnum_cmp(a, b) < 0, "10 < 20");
    ASSERT(fixnum_cmp(b, a) > 0, "20 > 10");
    ASSERT(fixnum_cmp(a, c) == 0, "10 == 10");

    ASSERT(fixnum_lt(a, b), "10 < 20");
    ASSERT(fixnum_gt(b, a), "20 > 10");
    ASSERT(fixnum_eq(a, c), "10 == 10");
    ASSERT(fixnum_le(a, c), "10 <= 10");
    ASSERT(fixnum_ge(a, c), "10 >= 10");

    return NULL;
}

/* Test bitwise operations */
TEST(fixnum_bitwise) {
    Value a = make_fixnum(12);  /* 0b1100 */
    Value b = make_fixnum(10);  /* 0b1010 */
    Value result;

    result = fixnum_and(a, b);
    ASSERT_EQ(untag_fixnum(result), 8, "12 & 10 = 8 (0b1000)");

    result = fixnum_or(a, b);
    ASSERT_EQ(untag_fixnum(result), 14, "12 | 10 = 14 (0b1110)");

    result = fixnum_xor(a, b);
    ASSERT_EQ(untag_fixnum(result), 6, "12 ^ 10 = 6 (0b0110)");

    a = make_fixnum(5);
    result = fixnum_not(a);
    ASSERT_EQ(untag_fixnum(result), ~5LL, "~5");

    return NULL;
}

/* Test shift operations */
TEST(fixnum_shifts) {
    Value a = make_fixnum(5);
    Value result;

    result = fixnum_shl(a, 2);
    ASSERT_EQ(untag_fixnum(result), 20, "5 << 2 = 20");

    a = make_fixnum(20);
    result = fixnum_shr(a, 2);
    ASSERT_EQ(untag_fixnum(result), 5, "20 >> 2 = 5");

    /* Negative shift */
    a = make_fixnum(-8);
    result = fixnum_shr(a, 1);
    ASSERT_EQ(untag_fixnum(result), -4, "-8 >> 1 = -4 (arithmetic)");

    return NULL;
}

/* Test negation */
TEST(fixnum_negation) {
    Value a = make_fixnum(42);
    Value result;

    ASSERT(!fixnum_neg_checked(a, &result), "Negation should not overflow");
    ASSERT_EQ(untag_fixnum(result), -42, "-(42) = -42");

    a = make_fixnum(-100);
    ASSERT(!fixnum_neg_checked(a, &result), "Negation should not overflow");
    ASSERT_EQ(untag_fixnum(result), 100, "-(-100) = 100");

    return NULL;
}

/* Test absolute value */
TEST(fixnum_absolute) {
    Value a = make_fixnum(-42);
    Value result;

    ASSERT(!fixnum_abs_checked(a, &result), "Abs should not overflow");
    ASSERT_EQ(untag_fixnum(result), 42, "abs(-42) = 42");

    a = make_fixnum(42);
    ASSERT(!fixnum_abs_checked(a, &result), "Abs should not overflow");
    ASSERT_EQ(untag_fixnum(result), 42, "abs(42) = 42");

    return NULL;
}

/* Test utility predicates */
TEST(fixnum_predicates) {
    ASSERT(fixnum_is_zero(make_fixnum(0)), "0 is zero");
    ASSERT(!fixnum_is_zero(make_fixnum(1)), "1 is not zero");

    ASSERT(fixnum_is_positive(make_fixnum(5)), "5 is positive");
    ASSERT(!fixnum_is_positive(make_fixnum(-5)), "-5 is not positive");
    ASSERT(!fixnum_is_positive(make_fixnum(0)), "0 is not positive");

    ASSERT(fixnum_is_negative(make_fixnum(-5)), "-5 is negative");
    ASSERT(!fixnum_is_negative(make_fixnum(5)), "5 is not negative");
    ASSERT(!fixnum_is_negative(make_fixnum(0)), "0 is not negative");

    ASSERT(fixnum_is_even(make_fixnum(4)), "4 is even");
    ASSERT(!fixnum_is_even(make_fixnum(5)), "5 is not even");

    ASSERT(fixnum_is_odd(make_fixnum(5)), "5 is odd");
    ASSERT(!fixnum_is_odd(make_fixnum(4)), "4 is not odd");

    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(fixnum_addition);
    RUN_TEST(fixnum_addition_overflow);
    RUN_TEST(fixnum_subtraction);
    RUN_TEST(fixnum_subtraction_overflow);
    RUN_TEST(fixnum_multiplication);
    RUN_TEST(fixnum_multiplication_overflow);
    RUN_TEST(fixnum_division);
    RUN_TEST(fixnum_division_by_zero);
    RUN_TEST(fixnum_modulo);
    RUN_TEST(fixnum_comparison);
    RUN_TEST(fixnum_bitwise);
    RUN_TEST(fixnum_shifts);
    RUN_TEST(fixnum_negation);
    RUN_TEST(fixnum_absolute);
    RUN_TEST(fixnum_predicates);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing fixnum arithmetic operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
