/* Test bigint operations */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test.h"
#include "beerlang.h"
#include "bigint.h"

/* Test bigint creation from int64_t */
TEST(bigint_from_int64) {
    memory_init();

    Value big = bigint_from_int64(123456789);
    ASSERT(is_pointer(big), "Bigint should be a pointer");
    ASSERT_EQ(object_type(big), TYPE_BIGINT, "Type should be bigint");
    ASSERT_EQ(object_refcount(big), 1, "Initial refcount = 1");

    int64_t n;
    ASSERT(bigint_to_int64(big, &n), "Should convert back to int64");
    ASSERT_EQ(n, 123456789, "Value should match");

    object_release(big);
    memory_shutdown();
    return NULL;
}

/* Test bigint from string */
TEST(bigint_from_string) {
    memory_init();

    Value big = bigint_from_string("987654321", 10);
    ASSERT(is_pointer(big), "Bigint should be created");

    int64_t n;
    ASSERT(bigint_to_int64(big, &n), "Should convert to int64");
    ASSERT_EQ(n, 987654321, "Value should match");

    object_release(big);

    /* Test hex */
    big = bigint_from_string("DEADBEEF", 16);
    ASSERT(is_pointer(big), "Hex bigint should be created");
    ASSERT(bigint_to_int64(big, &n), "Should convert to int64");
    ASSERT_EQ(n, 0xDEADBEEF, "Hex value should match");

    object_release(big);

    /* Test invalid string */
    Value invalid = bigint_from_string("not a number", 10);
    ASSERT(is_nil(invalid), "Invalid string should return nil");

    memory_shutdown();
    return NULL;
}

/* Test fixnum promotion */
TEST(bigint_from_fixnum) {
    memory_init();

    Value fixnum = make_fixnum(42);
    Value big = bigint_from_fixnum(fixnum);

    int64_t n;
    ASSERT(bigint_to_int64(big, &n), "Should convert to int64");
    ASSERT_EQ(n, 42, "Value should match fixnum");

    object_release(big);
    memory_shutdown();
    return NULL;
}

/* Test bigint demotion to fixnum */
TEST(bigint_demotion) {
    memory_init();

    /* Small bigint should demote */
    Value big = bigint_from_int64(100);
    Value demoted = bigint_try_demote(big);

    ASSERT(is_fixnum(demoted), "Small bigint should demote to fixnum");
    ASSERT_EQ(untag_fixnum(demoted), 100, "Value should match");

    object_release(big);

    /* Very large bigint should not demote */
    big = bigint_from_string("99999999999999999999999999999", 10);
    Value not_demoted = bigint_try_demote(big);
    ASSERT(is_pointer(not_demoted), "Large bigint should stay as bigint");

    object_release(big);

    memory_shutdown();
    return NULL;
}

/* Test bigint addition */
TEST(bigint_addition) {
    memory_init();

    Value a = bigint_from_int64(1000000000000LL);
    Value b = bigint_from_int64(2000000000000LL);
    Value result = bigint_add(a, b);

    int64_t n;
    ASSERT(bigint_to_int64(result, &n), "Should convert result");
    ASSERT_EQ(n, 3000000000000LL, "1000000000000 + 2000000000000 = 3000000000000");

    object_release(a);
    object_release(b);
    object_release(result);

    memory_shutdown();
    return NULL;
}

/* Test bigint subtraction */
TEST(bigint_subtraction) {
    memory_init();

    Value a = bigint_from_int64(5000000000000LL);
    Value b = bigint_from_int64(2000000000000LL);
    Value result = bigint_sub(a, b);

    int64_t n;
    ASSERT(bigint_to_int64(result, &n), "Should convert result");
    ASSERT_EQ(n, 3000000000000LL, "5000000000000 - 2000000000000 = 3000000000000");

    object_release(a);
    object_release(b);
    object_release(result);

    memory_shutdown();
    return NULL;
}

/* Test bigint multiplication */
TEST(bigint_multiplication) {
    memory_init();

    Value a = bigint_from_int64(1000000000LL);
    Value b = bigint_from_int64(1000000000LL);
    Value result = bigint_mul(a, b);

    int64_t n;
    ASSERT(bigint_to_int64(result, &n), "Should convert result");
    ASSERT_EQ(n, 1000000000000000000LL, "1000000000 * 1000000000 = 10^18");

    object_release(a);
    object_release(b);
    object_release(result);

    memory_shutdown();
    return NULL;
}

/* Test bigint division */
TEST(bigint_division) {
    memory_init();

    Value a = bigint_from_int64(100);
    Value b = bigint_from_int64(7);
    Value quotient = bigint_div(a, b);

    int64_t n;
    ASSERT(bigint_to_int64(quotient, &n), "Should convert quotient");
    ASSERT_EQ(n, 14, "100 / 7 = 14 (truncated)");

    object_release(a);
    object_release(b);
    object_release(quotient);

    /* Test division by zero */
    a = bigint_from_int64(42);
    b = bigint_from_int64(0);
    Value invalid = bigint_div(a, b);
    ASSERT(is_nil(invalid), "Division by zero should return nil");

    object_release(a);
    object_release(b);

    memory_shutdown();
    return NULL;
}

/* Test bigint modulo */
TEST(bigint_modulo) {
    memory_init();

    Value a = bigint_from_int64(100);
    Value b = bigint_from_int64(7);
    Value remainder = bigint_mod(a, b);

    int64_t n;
    ASSERT(bigint_to_int64(remainder, &n), "Should convert remainder");
    ASSERT_EQ(n, 2, "100 % 7 = 2");

    object_release(a);
    object_release(b);
    object_release(remainder);

    memory_shutdown();
    return NULL;
}

/* Test bigint comparison */
TEST(bigint_comparison) {
    memory_init();

    Value a = bigint_from_int64(1000);
    Value b = bigint_from_int64(2000);
    Value c = bigint_from_int64(1000);

    ASSERT(bigint_cmp(a, b) < 0, "1000 < 2000");
    ASSERT(bigint_cmp(b, a) > 0, "2000 > 1000");
    ASSERT(bigint_cmp(a, c) == 0, "1000 == 1000");

    ASSERT(bigint_lt(a, b), "1000 < 2000");
    ASSERT(bigint_gt(b, a), "2000 > 1000");
    ASSERT(bigint_eq(a, c), "1000 == 1000");

    object_release(a);
    object_release(b);
    object_release(c);

    memory_shutdown();
    return NULL;
}

/* Test bigint negation */
TEST(bigint_negation) {
    memory_init();

    Value a = bigint_from_int64(12345);
    Value neg = bigint_neg(a);

    int64_t n;
    ASSERT(bigint_to_int64(neg, &n), "Should convert");
    ASSERT_EQ(n, -12345, "-(12345) = -12345");

    object_release(a);
    object_release(neg);

    memory_shutdown();
    return NULL;
}

/* Test bigint absolute value */
TEST(bigint_absolute) {
    memory_init();

    Value a = bigint_from_int64(-9999);
    Value abs_val = bigint_abs(a);

    int64_t n;
    ASSERT(bigint_to_int64(abs_val, &n), "Should convert");
    ASSERT_EQ(n, 9999, "abs(-9999) = 9999");

    object_release(a);
    object_release(abs_val);

    memory_shutdown();
    return NULL;
}

/* Test bigint predicates */
TEST(bigint_predicates) {
    memory_init();

    Value zero = bigint_from_int64(0);
    Value pos = bigint_from_int64(100);
    Value neg = bigint_from_int64(-100);
    Value even = bigint_from_int64(42);
    Value odd = bigint_from_int64(43);

    ASSERT(bigint_is_zero(zero), "0 is zero");
    ASSERT(!bigint_is_zero(pos), "100 is not zero");

    ASSERT(bigint_is_positive(pos), "100 is positive");
    ASSERT(!bigint_is_positive(neg), "-100 is not positive");

    ASSERT(bigint_is_negative(neg), "-100 is negative");
    ASSERT(!bigint_is_negative(pos), "100 is not negative");

    ASSERT(bigint_is_even(even), "42 is even");
    ASSERT(!bigint_is_even(odd), "43 is not even");

    ASSERT(bigint_is_odd(odd), "43 is odd");
    ASSERT(!bigint_is_odd(even), "42 is not odd");

    object_release(zero);
    object_release(pos);
    object_release(neg);
    object_release(even);
    object_release(odd);

    memory_shutdown();
    return NULL;
}

/* Test bigint to string */
TEST(bigint_to_string) {
    memory_init();

    Value big = bigint_from_int64(123456789);
    char* str = bigint_to_string(big, 10);

    ASSERT_STR_EQ(str, "123456789", "Decimal string should match");
    free(str);

    /* Test hex conversion */
    str = bigint_to_string(big, 16);
    ASSERT_STR_EQ(str, "75bcd15", "Hex string should match");
    free(str);

    object_release(big);

    memory_shutdown();
    return NULL;
}

/* Test very large numbers */
TEST(bigint_large_numbers) {
    memory_init();

    /* 2^100 */
    Value big = bigint_from_string("1267650600228229401496703205376", 10);
    ASSERT(is_pointer(big), "Should create very large bigint");

    /* Can't convert to int64 */
    int64_t n;
    ASSERT(!bigint_to_int64(big, &n), "Should not fit in int64");

    /* Should not demote */
    Value demoted = bigint_try_demote(big);
    ASSERT(is_pointer(demoted), "Should stay as bigint");

    object_release(big);

    memory_shutdown();
    return NULL;
}

/* Test memory management with bigints */
TEST(bigint_memory_management) {
    memory_init();

    MemoryStats initial = memory_stats();

    Value big = bigint_from_int64(999999);
    MemoryStats after_alloc = memory_stats();

    ASSERT_EQ(after_alloc.objects_alive - initial.objects_alive, 1,
              "One object allocated");

    object_release(big);
    MemoryStats after_free = memory_stats();

    ASSERT_EQ(after_free.objects_alive, initial.objects_alive,
              "Object freed");

    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(bigint_from_int64);
    RUN_TEST(bigint_from_string);
    RUN_TEST(bigint_from_fixnum);
    RUN_TEST(bigint_demotion);
    RUN_TEST(bigint_addition);
    RUN_TEST(bigint_subtraction);
    RUN_TEST(bigint_multiplication);
    RUN_TEST(bigint_division);
    RUN_TEST(bigint_modulo);
    RUN_TEST(bigint_comparison);
    RUN_TEST(bigint_negation);
    RUN_TEST(bigint_absolute);
    RUN_TEST(bigint_predicates);
    RUN_TEST(bigint_to_string);
    RUN_TEST(bigint_large_numbers);
    RUN_TEST(bigint_memory_management);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing bigint operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
