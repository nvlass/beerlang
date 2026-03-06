/* Test vector operations */

#include <stdio.h>
#include "test.h"
#include "beerlang.h"
#include "vector.h"

/* Test vector creation */
TEST(vector_creation) {
    memory_init();
    vector_init();

    Value vec = vector_create(10);
    ASSERT(is_vector(vec), "Should be a vector");
    ASSERT_EQ(vector_length(vec), 0, "Length should be 0");
    ASSERT_EQ(vector_capacity(vec), 10, "Capacity should be 10");
    ASSERT(vector_empty(vec), "Should be empty");

    object_release(vec);
    memory_shutdown();
    return NULL;
}

/* Test vector_from_array */
TEST(vector_from_array) {
    memory_init();
    vector_init();

    Value values[] = {
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3)
    };

    Value vec = vector_from_array(values, 3);
    ASSERT(is_vector(vec), "Should be a vector");
    ASSERT_EQ(vector_length(vec), 3, "Length should be 3");
    ASSERT(!vector_empty(vec), "Should not be empty");

    ASSERT_EQ(untag_fixnum(vector_get(vec, 0)), 1, "First element should be 1");
    ASSERT_EQ(untag_fixnum(vector_get(vec, 1)), 2, "Second element should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(vec, 2)), 3, "Third element should be 3");

    object_release(vec);
    memory_shutdown();
    return NULL;
}

/* Test vector_push and vector_pop */
TEST(vector_push_pop) {
    memory_init();
    vector_init();

    Value vec = vector_create(2);  /* Small capacity to test resizing */

    vector_push(vec, make_fixnum(10));
    vector_push(vec, make_fixnum(20));
    vector_push(vec, make_fixnum(30));  /* Should trigger resize */

    ASSERT_EQ(vector_length(vec), 3, "Length should be 3");
    ASSERT(vector_capacity(vec) >= 3, "Capacity should be at least 3");

    Value v3 = vector_pop(vec);
    Value v2 = vector_pop(vec);
    Value v1 = vector_pop(vec);

    ASSERT_EQ(untag_fixnum(v1), 10, "Should pop 10");
    ASSERT_EQ(untag_fixnum(v2), 20, "Should pop 20");
    ASSERT_EQ(untag_fixnum(v3), 30, "Should pop 30");

    ASSERT_EQ(vector_length(vec), 0, "Should be empty after pops");

    object_release(vec);
    memory_shutdown();
    return NULL;
}

/* Test vector_get and vector_set */
TEST(vector_get_set) {
    memory_init();
    vector_init();

    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vec = vector_from_array(values, 3);

    ASSERT_EQ(untag_fixnum(vector_get(vec, 0)), 1, "Get 0 should be 1");
    ASSERT_EQ(untag_fixnum(vector_get(vec, 1)), 2, "Get 1 should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(vec, 2)), 3, "Get 2 should be 3");

    vector_set(vec, 1, make_fixnum(42));
    ASSERT_EQ(untag_fixnum(vector_get(vec, 1)), 42, "Get 1 should be 42 after set");

    /* Out of bounds */
    Value oob = vector_get(vec, 10);
    ASSERT(is_nil(oob), "Out of bounds should return nil");

    object_release(vec);
    memory_shutdown();
    return NULL;
}

/* Test vector_first and vector_last */
TEST(vector_first_last) {
    memory_init();
    vector_init();

    Value values[] = {make_fixnum(10), make_fixnum(20), make_fixnum(30)};
    Value vec = vector_from_array(values, 3);

    ASSERT_EQ(untag_fixnum(vector_first(vec)), 10, "First should be 10");
    ASSERT_EQ(untag_fixnum(vector_last(vec)), 30, "Last should be 30");

    object_release(vec);
    memory_shutdown();
    return NULL;
}

/* Test vector_clear */
TEST(vector_clear) {
    memory_init();
    vector_init();

    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vec = vector_from_array(values, 3);

    ASSERT_EQ(vector_length(vec), 3, "Should have 3 elements");

    vector_clear(vec);
    ASSERT_EQ(vector_length(vec), 0, "Should be empty after clear");
    ASSERT(vector_empty(vec), "Should be empty");

    object_release(vec);
    memory_shutdown();
    return NULL;
}

/* Test vector_slice */
TEST(vector_slice) {
    memory_init();
    vector_init();

    Value values[] = {
        make_fixnum(0),
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3),
        make_fixnum(4)
    };
    Value vec = vector_from_array(values, 5);

    /* Slice [1, 4) */
    Value slice = vector_slice(vec, 1, 4);
    ASSERT_EQ(vector_length(slice), 3, "Slice length should be 3");
    ASSERT_EQ(untag_fixnum(vector_get(slice, 0)), 1, "Slice[0] should be 1");
    ASSERT_EQ(untag_fixnum(vector_get(slice, 1)), 2, "Slice[1] should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(slice, 2)), 3, "Slice[2] should be 3");

    object_release(vec);
    object_release(slice);
    memory_shutdown();
    return NULL;
}

/* Test vector_concat */
TEST(vector_concat) {
    memory_init();
    vector_init();

    Value vals1[] = {make_fixnum(1), make_fixnum(2)};
    Value vals2[] = {make_fixnum(3), make_fixnum(4)};

    Value vec1 = vector_from_array(vals1, 2);
    Value vec2 = vector_from_array(vals2, 2);
    Value concat = vector_concat(vec1, vec2);

    ASSERT_EQ(vector_length(concat), 4, "Concatenated length should be 4");
    ASSERT_EQ(untag_fixnum(vector_get(concat, 0)), 1, "Element 0 should be 1");
    ASSERT_EQ(untag_fixnum(vector_get(concat, 1)), 2, "Element 1 should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(concat, 2)), 3, "Element 2 should be 3");
    ASSERT_EQ(untag_fixnum(vector_get(concat, 3)), 4, "Element 3 should be 4");

    object_release(vec1);
    object_release(vec2);
    object_release(concat);
    memory_shutdown();
    return NULL;
}

/* Test vector_clone */
TEST(vector_clone) {
    memory_init();
    vector_init();

    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vec = vector_from_array(values, 3);
    Value clone = vector_clone(vec);

    ASSERT(is_vector(clone), "Clone should be a vector");
    ASSERT_EQ(vector_length(clone), 3, "Clone length should be 3");
    ASSERT(vector_equal(vec, clone), "Clone should equal original");

    /* Modify clone shouldn't affect original */
    vector_set(clone, 0, make_fixnum(42));
    ASSERT_EQ(untag_fixnum(vector_get(vec, 0)), 1, "Original should still be 1");
    ASSERT_EQ(untag_fixnum(vector_get(clone, 0)), 42, "Clone should be 42");

    object_release(vec);
    object_release(clone);
    memory_shutdown();
    return NULL;
}

/* Test vector_equal */
TEST(vector_equality) {
    memory_init();
    vector_init();

    Value vals1[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vals2[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vals3[] = {make_fixnum(1), make_fixnum(2), make_fixnum(4)};

    Value vec1 = vector_from_array(vals1, 3);
    Value vec2 = vector_from_array(vals2, 3);
    Value vec3 = vector_from_array(vals3, 3);

    ASSERT(vector_equal(vec1, vec2), "Equal vectors should be equal");
    ASSERT(!vector_equal(vec1, vec3), "Different vectors should not be equal");
    ASSERT(vector_equal(vec1, vec1), "Vector should equal itself");

    object_release(vec1);
    object_release(vec2);
    object_release(vec3);
    memory_shutdown();
    return NULL;
}

/* Test vector_map */
static Value double_value(Value v) {
    if (is_fixnum(v)) {
        return make_fixnum(untag_fixnum(v) * 2);
    }
    return v;
}

TEST(vector_map) {
    memory_init();
    vector_init();

    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vec = vector_from_array(values, 3);
    Value mapped = vector_map(vec, double_value);

    ASSERT_EQ(vector_length(mapped), 3, "Mapped length should be 3");
    ASSERT_EQ(untag_fixnum(vector_get(mapped, 0)), 2, "Element 0 should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(mapped, 1)), 4, "Element 1 should be 4");
    ASSERT_EQ(untag_fixnum(vector_get(mapped, 2)), 6, "Element 2 should be 6");

    object_release(vec);
    object_release(mapped);
    memory_shutdown();
    return NULL;
}

/* Test vector_filter */
static bool is_even_fixnum(Value v) {
    return is_fixnum(v) && (untag_fixnum(v) % 2 == 0);
}

TEST(vector_filter) {
    memory_init();
    vector_init();

    Value values[] = {
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3),
        make_fixnum(4),
        make_fixnum(5)
    };
    Value vec = vector_from_array(values, 5);
    Value filtered = vector_filter(vec, is_even_fixnum);

    ASSERT_EQ(vector_length(filtered), 2, "Filtered length should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(filtered, 0)), 2, "Element 0 should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(filtered, 1)), 4, "Element 1 should be 4");

    object_release(vec);
    object_release(filtered);
    memory_shutdown();
    return NULL;
}

/* Test vector_fold */
static Value sum_fixnums(Value acc, Value elem) {
    if (is_fixnum(acc) && is_fixnum(elem)) {
        return make_fixnum(untag_fixnum(acc) + untag_fixnum(elem));
    }
    return acc;
}

TEST(vector_fold) {
    memory_init();
    vector_init();

    Value values[] = {
        make_fixnum(1),
        make_fixnum(2),
        make_fixnum(3),
        make_fixnum(4)
    };
    Value vec = vector_from_array(values, 4);
    Value sum = vector_fold(vec, make_fixnum(0), sum_fixnums);

    ASSERT_EQ(untag_fixnum(sum), 10, "Sum should be 10");

    object_release(vec);
    memory_shutdown();
    return NULL;
}

/* Test vector with heap objects */
TEST(vector_with_heap_objects) {
    memory_init();
    vector_init();

    MemoryStats initial = memory_stats();

    Value str1 = string_from_cstr("hello");
    Value str2 = string_from_cstr("world");

    Value vec = vector_create(2);
    vector_push(vec, str1);
    vector_push(vec, str2);

    /* Refcounts should be incremented */
    ASSERT_EQ(object_refcount(str1), 2, "String1 refcount should be 2");
    ASSERT_EQ(object_refcount(str2), 2, "String2 refcount should be 2");

    /* Release vector */
    object_release(vec);

    /* Release our references */
    object_release(str1);
    object_release(str2);

    MemoryStats after = memory_stats();

    /* All objects should be freed */
    ASSERT_EQ(after.objects_alive, initial.objects_alive, "All objects should be freed");

    memory_shutdown();
    return NULL;
}

/* Test conversion to/from lists */
TEST(vector_list_conversion) {
    memory_init();
    vector_init();
    cons_init();

    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};

    /* Vector to list */
    Value vec = vector_from_array(values, 3);
    Value list = vector_to_list(vec);

    ASSERT(is_cons(list), "Should be a list");
    ASSERT_EQ(list_length(list), 3, "List length should be 3");
    ASSERT_EQ(untag_fixnum(list_nth(list, 0)), 1, "List[0] should be 1");
    ASSERT_EQ(untag_fixnum(list_nth(list, 1)), 2, "List[1] should be 2");
    ASSERT_EQ(untag_fixnum(list_nth(list, 2)), 3, "List[2] should be 3");

    /* List back to vector */
    Value vec2 = vector_from_list(list);
    ASSERT(is_vector(vec2), "Should be a vector");
    ASSERT_EQ(vector_length(vec2), 3, "Vector length should be 3");
    ASSERT(vector_equal(vec, vec2), "Vectors should be equal");

    object_release(vec);
    object_release(list);
    object_release(vec2);
    memory_shutdown();
    return NULL;
}

/* Test memory management */
TEST(vector_memory_management) {
    memory_init();
    vector_init();

    MemoryStats initial = memory_stats();

    /* Create a vector with elements */
    Value values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
    Value vec = vector_from_array(values, 3);

    MemoryStats after_alloc = memory_stats();

    /* Should have allocated 1 vector */
    ASSERT_EQ(after_alloc.objects_alive - initial.objects_alive, 1,
              "Should have 1 object (vector)");

    /* Release the vector */
    object_release(vec);

    MemoryStats after_free = memory_stats();

    /* Vector should be freed */
    ASSERT_EQ(after_free.objects_alive, initial.objects_alive,
              "Vector should be freed");

    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(vector_creation);
    RUN_TEST(vector_from_array);
    RUN_TEST(vector_push_pop);
    RUN_TEST(vector_get_set);
    RUN_TEST(vector_first_last);
    RUN_TEST(vector_clear);
    RUN_TEST(vector_slice);
    RUN_TEST(vector_concat);
    RUN_TEST(vector_clone);
    RUN_TEST(vector_equality);
    RUN_TEST(vector_map);
    RUN_TEST(vector_filter);
    RUN_TEST(vector_fold);
    RUN_TEST(vector_with_heap_objects);
    RUN_TEST(vector_list_conversion);
    RUN_TEST(vector_memory_management);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing vector operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
