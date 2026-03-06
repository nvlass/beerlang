/* Test value representation and tagging */

#include <stdio.h>
#include "test.h"
#include "beerlang.h"
#include "cons.h"

/* Test fixnum tagging */
TEST(fixnum_tagging) {
    Value v = make_fixnum(42);
    ASSERT(is_fixnum(v), "Should be fixnum");
    ASSERT_EQ(untag_fixnum(v), 42, "Should extract correct value");

    v = make_fixnum(-100);
    ASSERT(is_fixnum(v), "Should be fixnum");
    ASSERT_EQ(untag_fixnum(v), -100, "Should handle negative numbers");

    return NULL;
}

/* Test character tagging */
TEST(char_tagging) {
    Value v = make_char('A');
    ASSERT(is_char(v), "Should be char");
    ASSERT_EQ(untag_char(v), 'A', "Should extract correct char");

    v = make_char(0x03BB);  /* λ */
    ASSERT(is_char(v), "Should be char");
    ASSERT_EQ(untag_char(v), 0x03BB, "Should handle Unicode");

    return NULL;
}

/* Test special constants */
TEST(special_constants) {
    ASSERT(is_nil(VALUE_NIL), "nil should be nil");
    ASSERT(is_true(VALUE_TRUE), "true should be true");
    ASSERT(is_false(VALUE_FALSE), "false should be false");

    ASSERT(!is_nil(VALUE_TRUE), "true should not be nil");
    ASSERT(!is_true(VALUE_FALSE), "false should not be true");

    return NULL;
}

/* Test tag detection */
TEST(tag_detection) {
    Value fixnum = make_fixnum(42);
    Value chr = make_char('X');
    Value nil = VALUE_NIL;

    ASSERT(is_fixnum(fixnum), "fixnum should be detected");
    ASSERT(is_char(chr), "char should be detected");
    ASSERT(is_special(nil), "special should be detected");

    ASSERT(!is_char(fixnum), "fixnum should not be char");
    ASSERT(!is_fixnum(chr), "char should not be fixnum");

    return NULL;
}

/* Test fixnum range */
TEST(fixnum_range) {
    /* Test large positive */
    Value v = make_fixnum(1000000000);
    ASSERT(is_fixnum(v), "Should be fixnum");
    ASSERT_EQ(untag_fixnum(v), 1000000000, "Should handle large positive");

    /* Test large negative */
    v = make_fixnum(-1000000000);
    ASSERT(is_fixnum(v), "Should be fixnum");
    ASSERT_EQ(untag_fixnum(v), -1000000000, "Should handle large negative");

    /* Test zero */
    v = make_fixnum(0);
    ASSERT(is_fixnum(v), "Should be fixnum");
    ASSERT_EQ(untag_fixnum(v), 0, "Should handle zero");

    return NULL;
}

/* Test that different values have different representations */
TEST(value_uniqueness) {
    Value v1 = make_fixnum(42);
    Value v2 = make_fixnum(43);
    ASSERT(!value_identical(v1, v2), "Different fixnums should be different");

    Value c1 = make_char('A');
    Value c2 = make_char('B');
    ASSERT(!value_identical(c1, c2), "Different chars should be different");

    ASSERT(!value_identical(VALUE_NIL, VALUE_TRUE), "nil != true");
    ASSERT(!value_identical(VALUE_TRUE, VALUE_FALSE), "true != false");

    return NULL;
}

/* Test value equality */
TEST(value_equality) {
    Value v1 = make_fixnum(42);
    Value v2 = make_fixnum(42);
    ASSERT(value_equal(v1, v2), "Same fixnums should be equal");

    v1 = make_char('X');
    v2 = make_char('X');
    ASSERT(value_equal(v1, v2), "Same chars should be equal");

    ASSERT(value_equal(VALUE_NIL, VALUE_NIL), "nil equals nil");
    ASSERT(value_equal(VALUE_TRUE, VALUE_TRUE), "true equals true");

    v1 = make_fixnum(42);
    v2 = make_char('*');
    ASSERT(!value_equal(v1, v2), "Different types not equal");

    return NULL;
}

/* Test value validation */
TEST(value_validation) {
    ASSERT(value_valid(VALUE_NIL), "nil should be valid");
    ASSERT(value_valid(VALUE_TRUE), "true should be valid");
    ASSERT(value_valid(VALUE_FALSE), "false should be valid");
    ASSERT(value_valid(make_fixnum(42)), "fixnum should be valid");
    ASSERT(value_valid(make_char('A')), "char should be valid");

    /* Invalid tag value */
    Value invalid;
    invalid.tag = (ValueTag)99;  /* Invalid tag */
    invalid.as.fixnum = 0;
    ASSERT(!value_valid(invalid), "Invalid tag should fail");

    return NULL;
}

/* Test type names */
TEST(type_names) {
    ASSERT_STR_EQ(value_type_name(VALUE_NIL), "nil", "nil type name");
    ASSERT_STR_EQ(value_type_name(VALUE_TRUE), "true", "true type name");
    ASSERT_STR_EQ(value_type_name(VALUE_FALSE), "false", "false type name");
    ASSERT_STR_EQ(value_type_name(make_fixnum(42)), "fixnum", "fixnum type name");
    ASSERT_STR_EQ(value_type_name(make_char('A')), "char", "char type name");

    return NULL;
}

/* Test cross-type sequence equality */
TEST(seq_equality) {
    memory_init();
    symbol_init();

    /* list = vector with same elements */
    Value list = cons(make_fixnum(1), cons(make_fixnum(2), cons(make_fixnum(3), VALUE_NIL)));
    Value vec = vector_create(3);
    vector_push(vec, make_fixnum(1));
    vector_push(vec, make_fixnum(2));
    vector_push(vec, make_fixnum(3));

    ASSERT(value_equal(list, vec), "list and vector with same elements should be equal");
    ASSERT(value_equal(vec, list), "vector and list with same elements should be equal");

    /* Different lengths */
    Value short_vec = vector_create(2);
    vector_push(short_vec, make_fixnum(1));
    vector_push(short_vec, make_fixnum(2));
    ASSERT(!value_equal(list, short_vec), "Different length sequences should not be equal");

    /* Different elements */
    Value diff_vec = vector_create(3);
    vector_push(diff_vec, make_fixnum(1));
    vector_push(diff_vec, make_fixnum(2));
    vector_push(diff_vec, make_fixnum(99));
    ASSERT(!value_equal(list, diff_vec), "Different element sequences should not be equal");

    /* Empty sequences */
    Value empty_vec = vector_create(0);
    ASSERT(value_equal(VALUE_NIL, empty_vec), "nil and empty vector should be equal");

    object_release(list);
    object_release(vec);
    object_release(short_vec);
    object_release(diff_vec);
    object_release(empty_vec);

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(fixnum_tagging);
    RUN_TEST(char_tagging);
    RUN_TEST(special_constants);
    RUN_TEST(tag_detection);
    RUN_TEST(fixnum_range);
    RUN_TEST(value_uniqueness);
    RUN_TEST(value_equality);
    RUN_TEST(value_validation);
    RUN_TEST(type_names);
    RUN_TEST(seq_equality);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing tagged pointer implementation...\n");
    RUN_SUITE(all_tests);
    return 0;
}
