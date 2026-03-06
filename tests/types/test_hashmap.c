/* Test hashmap operations (HAMT-based persistent hashmap) */

#include <stdio.h>
#include "test.h"
#include "beerlang.h"
#include "hashmap.h"

/* Test hashmap creation */
TEST(hashmap_creation) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();
    ASSERT(is_hashmap(map), "Should be a hashmap");
    ASSERT_EQ(hashmap_size(map), 0, "Size should be 0");
    ASSERT(hashmap_empty(map), "Should be empty");

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test basic get/set operations */
TEST(hashmap_get_set) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    /* Set some key-value pairs (mutating for convenience) */
    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));
    hashmap_set(map, make_fixnum(3), make_fixnum(300));

    ASSERT_EQ(hashmap_size(map), 3, "Size should be 3");

    /* Get values */
    ASSERT_EQ(untag_fixnum(hashmap_get(map, make_fixnum(1))), 100, "Key 1 should be 100");
    ASSERT_EQ(untag_fixnum(hashmap_get(map, make_fixnum(2))), 200, "Key 2 should be 200");
    ASSERT_EQ(untag_fixnum(hashmap_get(map, make_fixnum(3))), 300, "Key 3 should be 300");

    /* Non-existent key */
    Value missing = hashmap_get(map, make_fixnum(999));
    ASSERT(is_nil(missing), "Missing key should return nil");

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_contains */
TEST(hashmap_contains) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(42), make_fixnum(123));

    ASSERT(hashmap_contains(map, make_fixnum(42)), "Should contain key 42");
    ASSERT(!hashmap_contains(map, make_fixnum(99)), "Should not contain key 99");

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test overwriting existing key */
TEST(hashmap_update) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    ASSERT_EQ(untag_fixnum(hashmap_get(map, make_fixnum(1))), 100, "Initial value should be 100");

    /* Overwrite */
    hashmap_set(map, make_fixnum(1), make_fixnum(999));
    ASSERT_EQ(untag_fixnum(hashmap_get(map, make_fixnum(1))), 999, "Updated value should be 999");
    ASSERT_EQ(hashmap_size(map), 1, "Size should still be 1");

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_remove */
TEST(hashmap_remove) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));
    hashmap_set(map, make_fixnum(3), make_fixnum(300));

    ASSERT_EQ(hashmap_size(map), 3, "Should have 3 entries");

    hashmap_remove(map, make_fixnum(2));

    ASSERT_EQ(hashmap_size(map), 2, "Should have 2 entries after remove");
    ASSERT(!hashmap_contains(map, make_fixnum(2)), "Key 2 should be removed");
    ASSERT(hashmap_contains(map, make_fixnum(1)), "Key 1 should still exist");
    ASSERT(hashmap_contains(map, make_fixnum(3)), "Key 3 should still exist");

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test many entries (exercises HAMT tree depth) */
TEST(hashmap_many_entries) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    for (int i = 0; i < 100; i++) {
        hashmap_set(map, make_fixnum(i), make_fixnum(i * 10));
    }

    ASSERT_EQ(hashmap_size(map), 100, "Should have 100 entries");

    /* Verify all values */
    for (int i = 0; i < 100; i++) {
        Value val = hashmap_get(map, make_fixnum(i));
        ASSERT_EQ(untag_fixnum(val), i * 10, "Value should be preserved");
    }

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_keys */
TEST(hashmap_keys) {
    memory_init();
    hashmap_init();
    vector_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));
    hashmap_set(map, make_fixnum(3), make_fixnum(300));

    Value keys = hashmap_keys(map);
    ASSERT(is_vector(keys), "Keys should be a vector");
    ASSERT_EQ(vector_length(keys), 3, "Should have 3 keys");

    /* Check all expected keys are present (order may vary) */
    bool found1 = false, found2 = false, found3 = false;
    for (size_t i = 0; i < vector_length(keys); i++) {
        Value key = vector_get(keys, i);
        if (is_fixnum(key)) {
            int64_t n = untag_fixnum(key);
            if (n == 1) found1 = true;
            if (n == 2) found2 = true;
            if (n == 3) found3 = true;
        }
    }

    ASSERT(found1 && found2 && found3, "All keys should be present");

    object_release(map);
    object_release(keys);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_values */
TEST(hashmap_values) {
    memory_init();
    hashmap_init();
    vector_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));

    Value values = hashmap_values(map);
    ASSERT(is_vector(values), "Values should be a vector");
    ASSERT_EQ(vector_length(values), 2, "Should have 2 values");

    object_release(map);
    object_release(values);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_entries */
TEST(hashmap_entries) {
    memory_init();
    hashmap_init();
    vector_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));

    Value entries = hashmap_entries(map);
    ASSERT(is_vector(entries), "Entries should be a vector");
    ASSERT_EQ(vector_length(entries), 2, "Should have 2 entries");

    /* Each entry should be a [key value] vector */
    Value entry = vector_get(entries, 0);
    ASSERT(is_vector(entry), "Entry should be a vector");
    ASSERT_EQ(vector_length(entry), 2, "Entry should have 2 elements");

    object_release(map);
    object_release(entries);
    memory_shutdown();
    return NULL;
}

/* Test persistent assoc (structural sharing) */
TEST(hashmap_assoc) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();
    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));

    /* assoc returns new map, original unchanged */
    Value map2 = hashmap_assoc(map, make_fixnum(3), make_fixnum(300));

    ASSERT(is_hashmap(map2), "Result should be a hashmap");
    ASSERT_EQ(hashmap_size(map), 2, "Original should still have 2 entries");
    ASSERT_EQ(hashmap_size(map2), 3, "New map should have 3 entries");
    ASSERT_EQ(untag_fixnum(hashmap_get(map2, make_fixnum(3))), 300, "New key should be present");
    ASSERT(!hashmap_contains(map, make_fixnum(3)), "Original should not have new key");

    object_release(map);
    object_release(map2);
    memory_shutdown();
    return NULL;
}

/* Test persistent dissoc */
TEST(hashmap_dissoc) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();
    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));
    hashmap_set(map, make_fixnum(3), make_fixnum(300));

    Value map2 = hashmap_dissoc(map, make_fixnum(2));

    ASSERT_EQ(hashmap_size(map), 3, "Original should still have 3 entries");
    ASSERT_EQ(hashmap_size(map2), 2, "New map should have 2 entries");
    ASSERT(hashmap_contains(map, make_fixnum(2)), "Original should still have key 2");
    ASSERT(!hashmap_contains(map2, make_fixnum(2)), "New map should not have key 2");

    object_release(map);
    object_release(map2);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_merge */
TEST(hashmap_merge) {
    memory_init();
    hashmap_init();

    Value map1 = hashmap_create_default();
    Value map2 = hashmap_create_default();

    hashmap_set(map1, make_fixnum(1), make_fixnum(100));
    hashmap_set(map1, make_fixnum(2), make_fixnum(200));

    hashmap_set(map2, make_fixnum(2), make_fixnum(999));  /* Overwrite key 2 */
    hashmap_set(map2, make_fixnum(3), make_fixnum(300));

    Value merged = hashmap_merge(map1, map2);

    ASSERT_EQ(hashmap_size(merged), 3, "Merged should have 3 entries");
    ASSERT_EQ(untag_fixnum(hashmap_get(merged, make_fixnum(1))), 100, "Key 1 should be 100");
    ASSERT_EQ(untag_fixnum(hashmap_get(merged, make_fixnum(2))), 999, "Key 2 should be 999 (from map2)");
    ASSERT_EQ(untag_fixnum(hashmap_get(merged, make_fixnum(3))), 300, "Key 3 should be 300");

    object_release(map1);
    object_release(map2);
    object_release(merged);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_equal */
TEST(hashmap_equality) {
    memory_init();
    hashmap_init();

    Value map1 = hashmap_create_default();
    Value map2 = hashmap_create_default();
    Value map3 = hashmap_create_default();

    hashmap_set(map1, make_fixnum(1), make_fixnum(100));
    hashmap_set(map1, make_fixnum(2), make_fixnum(200));

    hashmap_set(map2, make_fixnum(1), make_fixnum(100));
    hashmap_set(map2, make_fixnum(2), make_fixnum(200));

    hashmap_set(map3, make_fixnum(1), make_fixnum(100));
    hashmap_set(map3, make_fixnum(2), make_fixnum(999));  /* Different value */

    ASSERT(hashmap_equal(map1, map2), "Equal hashmaps should be equal");
    ASSERT(!hashmap_equal(map1, map3), "Different hashmaps should not be equal");
    ASSERT(hashmap_equal(map1, map1), "Hashmap should equal itself");

    object_release(map1);
    object_release(map2);
    object_release(map3);
    memory_shutdown();
    return NULL;
}

/* Test hashmap with string keys */
TEST(hashmap_string_keys) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    Value key1 = string_from_cstr("hello");
    Value key2 = string_from_cstr("world");

    hashmap_set(map, key1, make_fixnum(100));
    hashmap_set(map, key2, make_fixnum(200));

    ASSERT_EQ(hashmap_size(map), 2, "Should have 2 entries");
    ASSERT_EQ(untag_fixnum(hashmap_get(map, key1)), 100, "Key 'hello' should be 100");
    ASSERT_EQ(untag_fixnum(hashmap_get(map, key2)), 200, "Key 'world' should be 200");

    object_release(map);
    object_release(key1);
    object_release(key2);
    memory_shutdown();
    return NULL;
}

/* Test hashmap with heap objects as values */
TEST(hashmap_with_heap_objects) {
    memory_init();
    hashmap_init();

    Value str1 = string_from_cstr("value1");
    Value str2 = string_from_cstr("value2");

    Value map = hashmap_create_default();
    hashmap_set(map, make_fixnum(1), str1);
    hashmap_set(map, make_fixnum(2), str2);

    /* Refcounts: str has 1 (our ref) + 1 (HAMT node retains) = 2 */
    ASSERT_EQ(object_refcount(str1), 2, "String1 refcount should be 2");
    ASSERT_EQ(object_refcount(str2), 2, "String2 refcount should be 2");

    /* Release map */
    object_release(map);

    /* Release our references */
    object_release(str1);
    object_release(str2);

    memory_shutdown();
    return NULL;
}

/* Test iteration */
static int iter_count = 0;
static void count_iter(Value key, Value value, void* ctx) {
    (void)key;
    (void)value;
    (void)ctx;
    iter_count++;
}

TEST(hashmap_iteration) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(1), make_fixnum(100));
    hashmap_set(map, make_fixnum(2), make_fixnum(200));
    hashmap_set(map, make_fixnum(3), make_fixnum(300));

    iter_count = 0;
    hashmap_foreach(map, count_iter, NULL);

    ASSERT_EQ(iter_count, 3, "Should iterate over 3 entries");

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test hashmap_get_default */
TEST(hashmap_get_default) {
    memory_init();
    hashmap_init();

    Value map = hashmap_create_default();

    hashmap_set(map, make_fixnum(1), make_fixnum(100));

    Value val1 = hashmap_get_default(map, make_fixnum(1), make_fixnum(999));
    ASSERT_EQ(untag_fixnum(val1), 100, "Existing key should return actual value");

    Value val2 = hashmap_get_default(map, make_fixnum(999), make_fixnum(42));
    ASSERT_EQ(untag_fixnum(val2), 42, "Missing key should return default value");

    object_release(map);
    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(hashmap_creation);
    RUN_TEST(hashmap_get_set);
    RUN_TEST(hashmap_contains);
    RUN_TEST(hashmap_update);
    RUN_TEST(hashmap_remove);
    RUN_TEST(hashmap_many_entries);
    RUN_TEST(hashmap_keys);
    RUN_TEST(hashmap_values);
    RUN_TEST(hashmap_entries);
    RUN_TEST(hashmap_assoc);
    RUN_TEST(hashmap_dissoc);
    RUN_TEST(hashmap_merge);
    RUN_TEST(hashmap_equality);
    RUN_TEST(hashmap_string_keys);
    RUN_TEST(hashmap_with_heap_objects);
    RUN_TEST(hashmap_iteration);
    RUN_TEST(hashmap_get_default);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing hashmap operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
