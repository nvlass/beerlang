/* Test cons cell and list operations */

#include <stdio.h>
#include "test.h"
#include "beerlang.h"
#include "cons.h"

/* Test basic cons cell creation */
TEST(cons_creation) {
    memory_init();
    cons_init();

    Value a = make_fixnum(1);
    Value b = make_fixnum(2);
    Value c = cons(a, b);

    ASSERT(is_cons(c), "Should be a cons cell");
    ASSERT_EQ(object_type(c), TYPE_CONS, "Type should be cons");
    ASSERT_EQ(untag_fixnum(car(c)), 1, "Car should be 1");
    ASSERT_EQ(untag_fixnum(cdr(c)), 2, "Cdr should be 2");

    object_release(c);
    memory_shutdown();
    return NULL;
}

/* Test cons with heap objects */
TEST(cons_with_heap_objects) {
    memory_init();
    cons_init();

    MemoryStats initial = memory_stats();

    Value str = string_from_cstr("hello");
    Value num = bigint_from_int64(1000);
    Value c = cons(str, num);

    ASSERT(is_cons(c), "Should be a cons cell");

    /* Refcounts should be incremented when stored in cons */
    ASSERT_EQ(object_refcount(str), 2, "String refcount should be 2 (owned by test and cons)");
    ASSERT_EQ(object_refcount(num), 2, "Bigint refcount should be 2 (owned by test and cons)");

    /* Release the cons - this should release the cons's references to str and num */
    object_release(c);

    /* Now release our references */
    object_release(str);
    object_release(num);

    MemoryStats after = memory_stats();

    /* All objects should be freed */
    ASSERT_EQ(after.objects_alive, initial.objects_alive, "All objects should be freed");

    memory_shutdown();
    return NULL;
}

/* Test empty list */
TEST(empty_list) {
    memory_init();
    cons_init();

    Value empty = list_empty();

    ASSERT(is_nil(empty), "Empty list should be nil");
    ASSERT(is_empty_list(empty), "Should be empty");
    ASSERT(is_proper_list(empty), "Empty list is a proper list");
    ASSERT_EQ(list_length(empty), 0, "Length should be 0");

    memory_shutdown();
    return NULL;
}

/* Test list creation from array */
TEST(list_from_array) {
    memory_init();
    cons_init();

    Value values[] = {
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3)
    };

    Value list = list_from_array(values, 3);

    ASSERT(is_cons(list), "Should be a cons");
    ASSERT(is_proper_list(list), "Should be a proper list");
    ASSERT_EQ(list_length(list), 3, "Length should be 3");

    ASSERT_EQ(untag_fixnum(list_nth(list, 0)), 1, "First element should be 1");
    ASSERT_EQ(untag_fixnum(list_nth(list, 1)), 2, "Second element should be 2");
    ASSERT_EQ(untag_fixnum(list_nth(list, 2)), 3, "Third element should be 3");

    object_release(list);
    memory_shutdown();
    return NULL;
}

/* Test proper vs improper lists */
TEST(proper_vs_improper_lists) {
    memory_init();
    cons_init();

    /* Proper list: (1 2 3) */
    Value proper = cons(make_fixnum(1),
                       cons(make_fixnum(2),
                           cons(make_fixnum(3), VALUE_NIL)));

    ASSERT(is_proper_list(proper), "Should be a proper list");
    ASSERT_EQ(list_length(proper), 3, "Length should be 3");

    /* Improper list: (1 . 2) */
    Value improper = cons(make_fixnum(1), make_fixnum(2));

    ASSERT(!is_proper_list(improper), "Should not be a proper list");
    ASSERT_EQ(list_length(improper), -1, "Length should be -1 for improper list");

    object_release(proper);
    object_release(improper);

    memory_shutdown();
    return NULL;
}

/* Test list access functions */
TEST(list_access) {
    memory_init();
    cons_init();

    Value values[] = {
        make_fixnum(10),
        make_fixnum(20),
        make_fixnum(30)
    };
    Value list = list_from_array(values, 3);

    ASSERT_EQ(untag_fixnum(list_first(list)), 10, "First should be 10");
    ASSERT_EQ(untag_fixnum(list_second(list)), 20, "Second should be 20");
    ASSERT_EQ(untag_fixnum(list_third(list)), 30, "Third should be 30");

    /* Out of bounds */
    Value oob = list_nth(list, 10);
    ASSERT(is_nil(oob), "Out of bounds should return nil");

    object_release(list);
    memory_shutdown();
    return NULL;
}

/* Test list reverse */
TEST(list_reverse) {
    memory_init();
    cons_init();

    Value values[] = {
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3)
    };
    Value list = list_from_array(values, 3);
    Value reversed = list_reverse(list);

    ASSERT_EQ(list_length(reversed), 3, "Reversed length should be 3");
    ASSERT_EQ(untag_fixnum(list_nth(reversed, 0)), 3, "First should be 3");
    ASSERT_EQ(untag_fixnum(list_nth(reversed, 1)), 2, "Second should be 2");
    ASSERT_EQ(untag_fixnum(list_nth(reversed, 2)), 1, "Third should be 1");

    /* Original list should be unchanged */
    ASSERT_EQ(untag_fixnum(list_nth(list, 0)), 1, "Original first should be 1");

    object_release(list);
    object_release(reversed);

    memory_shutdown();
    return NULL;
}

/* Test list append */
TEST(list_append) {
    memory_init();
    cons_init();

    Value vals1[] = {make_fixnum(1), make_fixnum(2)};
    Value vals2[] = {make_fixnum(3), make_fixnum(4)};

    Value list1 = list_from_array(vals1, 2);
    Value list2 = list_from_array(vals2, 2);
    Value appended = list_append(list1, list2);

    ASSERT_EQ(list_length(appended), 4, "Appended length should be 4");
    ASSERT_EQ(untag_fixnum(list_nth(appended, 0)), 1, "First should be 1");
    ASSERT_EQ(untag_fixnum(list_nth(appended, 1)), 2, "Second should be 2");
    ASSERT_EQ(untag_fixnum(list_nth(appended, 2)), 3, "Third should be 3");
    ASSERT_EQ(untag_fixnum(list_nth(appended, 3)), 4, "Fourth should be 4");

    object_release(list1);
    object_release(list2);
    object_release(appended);

    memory_shutdown();
    return NULL;
}

/* Test list map */
static Value double_value(Value v) {
    if (is_fixnum(v)) {
        return make_fixnum(untag_fixnum(v) * 2);
    }
    return v;
}

TEST(list_map) {
    memory_init();
    cons_init();

    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value list = list_from_array(values, 3);
    Value mapped = list_map(list, double_value);

    ASSERT_EQ(list_length(mapped), 3, "Mapped length should be 3");
    ASSERT_EQ(untag_fixnum(list_nth(mapped, 0)), 2, "First should be 2");
    ASSERT_EQ(untag_fixnum(list_nth(mapped, 1)), 4, "Second should be 4");
    ASSERT_EQ(untag_fixnum(list_nth(mapped, 2)), 6, "Third should be 6");

    object_release(list);
    object_release(mapped);

    memory_shutdown();
    return NULL;
}

/* Test list filter */
static bool is_even_fixnum(Value v) {
    return is_fixnum(v) && (untag_fixnum(v) % 2 == 0);
}

TEST(list_filter) {
    memory_init();
    cons_init();

    Value values[] = {
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3),
        make_fixnum(4),
        make_fixnum(5)
    };
    Value list = list_from_array(values, 5);
    Value filtered = list_filter(list, is_even_fixnum);

    ASSERT_EQ(list_length(filtered), 2, "Filtered length should be 2");
    ASSERT_EQ(untag_fixnum(list_nth(filtered, 0)), 2, "First should be 2");
    ASSERT_EQ(untag_fixnum(list_nth(filtered, 1)), 4, "Second should be 4");

    object_release(list);
    object_release(filtered);

    memory_shutdown();
    return NULL;
}

/* Test list fold */
static Value sum_fixnums(Value acc, Value elem) {
    if (is_fixnum(acc) && is_fixnum(elem)) {
        return make_fixnum(untag_fixnum(acc) + untag_fixnum(elem));
    }
    return acc;
}

TEST(list_fold) {
    memory_init();
    cons_init();

    Value values[] = {
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3),
        make_fixnum(4)
    };
    Value list = list_from_array(values, 4);
    Value sum = list_fold(list, make_fixnum(0), sum_fixnums);

    ASSERT_EQ(untag_fixnum(sum), 10, "Sum should be 10");

    object_release(list);

    memory_shutdown();
    return NULL;
}

/* Test list predicates */
static bool is_positive_fixnum(Value v) {
    return is_fixnum(v) && untag_fixnum(v) > 0;
}

TEST(list_predicates) {
    memory_init();
    cons_init();

    Value all_pos[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value some_pos[] = {make_fixnum(1), make_fixnum(-2), make_fixnum(3)};
    Value none_pos[] = {make_fixnum(-1), make_fixnum(-2), make_fixnum(-3)};

    Value list_all = list_from_array(all_pos, 3);
    Value list_some = list_from_array(some_pos, 3);
    Value list_none = list_from_array(none_pos, 3);

    ASSERT(list_every(list_all, is_positive_fixnum), "All should be positive");
    ASSERT(!list_every(list_some, is_positive_fixnum), "Not all should be positive");
    ASSERT(!list_every(list_none, is_positive_fixnum), "None should be positive");

    ASSERT(list_any(list_all, is_positive_fixnum), "Some should be positive");
    ASSERT(list_any(list_some, is_positive_fixnum), "Some should be positive");
    ASSERT(!list_any(list_none, is_positive_fixnum), "None should be positive");

    object_release(list_all);
    object_release(list_some);
    object_release(list_none);

    memory_shutdown();
    return NULL;
}

/* Test list equality */
TEST(list_equality) {
    memory_init();
    cons_init();

    Value vals1[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vals2[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vals3[] = {make_fixnum(1), make_fixnum(2), make_fixnum(4)};

    Value list1 = list_from_array(vals1, 3);
    Value list2 = list_from_array(vals2, 3);
    Value list3 = list_from_array(vals3, 3);

    ASSERT(list_equal(list1, list2), "Equal lists should be equal");
    ASSERT(!list_equal(list1, list3), "Different lists should not be equal");

    /* Same pointer */
    ASSERT(list_equal(list1, list1), "List should equal itself");

    /* Empty lists */
    ASSERT(list_equal(VALUE_NIL, VALUE_NIL), "Empty lists should be equal");

    object_release(list1);
    object_release(list2);
    object_release(list3);

    memory_shutdown();
    return NULL;
}

/* Test set_car and set_cdr */
TEST(cons_mutation) {
    memory_init();
    cons_init();

    Value c = cons(make_fixnum(1), make_fixnum(2));

    ASSERT_EQ(untag_fixnum(car(c)), 1, "Initial car should be 1");
    ASSERT_EQ(untag_fixnum(cdr(c)), 2, "Initial cdr should be 2");

    set_car(c, make_fixnum(10));
    ASSERT_EQ(untag_fixnum(car(c)), 10, "Car should be updated to 10");

    set_cdr(c, make_fixnum(20));
    ASSERT_EQ(untag_fixnum(cdr(c)), 20, "Cdr should be updated to 20");

    object_release(c);

    memory_shutdown();
    return NULL;
}

/* Test memory management */
TEST(cons_memory_management) {
    memory_init();
    cons_init();

    MemoryStats initial = memory_stats();

    /* Create a list */
    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value list = list_from_array(values, 3);

    MemoryStats after_alloc = memory_stats();

    /* Should have allocated 3 cons cells */
    ASSERT_EQ(after_alloc.objects_alive - initial.objects_alive, 3,
              "Should have 3 objects (cons cells)");

    /* Release the list */
    object_release(list);

    MemoryStats after_free = memory_stats();

    /* All cons cells should be freed */
    ASSERT_EQ(after_free.objects_alive, initial.objects_alive,
              "All cons cells should be freed");

    memory_shutdown();
    return NULL;
}

/* Test cons with mixed types */
TEST(cons_mixed_types) {
    memory_init();
    cons_init();

    Value str = string_from_cstr("hello");
    Value num = make_fixnum(42);
    Value big = bigint_from_int64(1000000);

    Value list = cons(str, cons(num, cons(big, VALUE_NIL)));

    ASSERT(is_proper_list(list), "Should be a proper list");
    ASSERT_EQ(list_length(list), 3, "Length should be 3");

    /* Check types */
    ASSERT_EQ(object_type(list_first(list)), TYPE_STRING, "First should be string");
    ASSERT(is_fixnum(list_second(list)), "Second should be fixnum");
    ASSERT_EQ(object_type(list_third(list)), TYPE_BIGINT, "Third should be bigint");

    object_release(list);
    object_release(str);
    object_release(big);

    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(cons_creation);
    RUN_TEST(cons_with_heap_objects);
    RUN_TEST(empty_list);
    RUN_TEST(list_from_array);
    RUN_TEST(proper_vs_improper_lists);
    RUN_TEST(list_access);
    RUN_TEST(list_reverse);
    RUN_TEST(list_append);
    RUN_TEST(list_map);
    RUN_TEST(list_filter);
    RUN_TEST(list_fold);
    RUN_TEST(list_predicates);
    RUN_TEST(list_equality);
    RUN_TEST(cons_mutation);
    RUN_TEST(cons_memory_management);
    RUN_TEST(cons_mixed_types);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing cons cell and list operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
