/* Test vector operations */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    /* A small vector is the wrapper plus its tail node */
    ASSERT_EQ(after_alloc.objects_alive - initial.objects_alive, 2,
              "Should have 2 objects (wrapper + tail node)");

    /* Release the vector */
    object_release(vec);

    MemoryStats after_free = memory_stats();

    /* Vector should be freed */
    ASSERT_EQ(after_free.objects_alive, initial.objects_alive,
              "Vector should be freed");

    memory_shutdown();
    return NULL;
}

/* ---- Persistent trie tests ------------------------------------------- */

static Value build_range(size_t n) {
    Value v = vector_create(0);
    for (size_t i = 0; i < n; i++) vector_push(v, make_fixnum((int64_t)i));
    return v;
}

static bool matches_range(Value v, size_t n) {
    if (vector_length(v) != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (untag_fixnum(vector_get(v, i)) != (int64_t)i) return false;
    }
    return true;
}

/* Sizes straddling every tail / leaf / tree-depth transition */
TEST(vector_trie_boundaries) {
    memory_init();
    vector_init();
    size_t initial = memory_stats().objects_alive;

    size_t sizes[] = {0, 1, 31, 32, 33, 63, 64, 65, 1023, 1024, 1025,
                      1055, 1056, 1057, 1088, 1089, 32768, 32800, 32801, 33825};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        Value v = build_range(sizes[s]);
        ASSERT(matches_range(v, sizes[s]), "push-built vector has every element");
        Value last = vector_last(v);
        ASSERT(sizes[s] == 0 ? is_nil(last) : untag_fixnum(last) == (int64_t)sizes[s] - 1,
               "last element");
        ASSERT(is_nil(vector_get(v, sizes[s])), "out of bounds is nil");
        object_release(v);
    }
    ASSERT_EQ(memory_stats().objects_alive, initial, "all trie nodes freed");
    memory_shutdown();
    return NULL;
}

TEST(vector_conj_persistence) {
    memory_init();
    vector_init();
    size_t initial = memory_stats().objects_alive;

    size_t sizes[] = {0, 5, 31, 32, 100, 1056, 1100};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        size_t n = sizes[s];
        Value base = build_range(n);
        Value a = vector_conj(base, make_fixnum(-1));
        Value b = vector_conj(base, make_fixnum(-2));
        ASSERT(matches_range(base, n), "conj leaves the original intact");
        ASSERT_EQ(vector_length(a), n + 1, "conj adds one");
        ASSERT_EQ(untag_fixnum(vector_get(a, n)), -1, "conj result A");
        ASSERT_EQ(untag_fixnum(vector_get(b, n)), -2, "conj result B diverges");
        for (size_t i = 0; i < n; i++) {
            ASSERT_EQ(untag_fixnum(vector_get(a, i)), (int64_t)i, "prefix shared");
        }
        object_release(base);
        object_release(a);
        object_release(b);
    }
    ASSERT_EQ(memory_stats().objects_alive, initial, "all nodes freed");
    memory_shutdown();
    return NULL;
}

TEST(vector_assoc_persistence) {
    memory_init();
    vector_init();
    size_t initial = memory_stats().objects_alive;

    Value base = build_range(1100);
    Value in_tree = vector_assoc_n(base, 5, make_fixnum(-5));
    Value in_tail = vector_assoc_n(base, 1090, make_fixnum(-6));
    Value appended = vector_assoc_n(base, 1100, make_fixnum(-7));
    Value beyond = vector_assoc_n(base, 1101, make_fixnum(-8));

    ASSERT(matches_range(base, 1100), "assoc leaves the original intact");
    ASSERT_EQ(untag_fixnum(vector_get(in_tree, 5)), -5, "assoc into tree");
    ASSERT_EQ(untag_fixnum(vector_get(in_tree, 6)), 6, "neighbour untouched");
    ASSERT_EQ(untag_fixnum(vector_get(in_tail, 1090)), -6, "assoc into tail");
    ASSERT_EQ(vector_length(appended), 1101, "assoc at count appends");
    ASSERT(is_nil(beyond), "assoc beyond count is nil");

    object_release(base);
    object_release(in_tree);
    object_release(in_tail);
    object_release(appended);
    ASSERT_EQ(memory_stats().objects_alive, initial, "all nodes freed");
    memory_shutdown();
    return NULL;
}

/* After an O(1) clone, interior nodes have refcount 1 but are reachable
 * from both vectors through the shared root; in-place ops must not
 * mutate them. */
TEST(vector_topdown_sharing) {
    memory_init();
    vector_init();
    size_t initial = memory_stats().objects_alive;

    /* 1100 elements: depth-2 tree (shift 10). Pushing 40 more into the
     * clone forces push_tail into shared interior nodes. */
    Value orig = build_range(1100);
    Value copy = vector_clone(orig);
    for (int i = 0; i < 40; i++) vector_push(copy, make_fixnum(1100 + i));
    vector_set(orig, 3, make_fixnum(-3));
    vector_set(orig, 1095, make_fixnum(-4));

    ASSERT_EQ(vector_length(orig), 1100, "original length unchanged");
    ASSERT_EQ(untag_fixnum(vector_get(orig, 3)), -3, "set applied to original");
    ASSERT_EQ(untag_fixnum(vector_get(orig, 1095)), -4, "tail set applied to original");
    ASSERT(matches_range(copy, 1140), "clone unaffected by sets on original");

    object_release(orig);
    object_release(copy);
    ASSERT_EQ(memory_stats().objects_alive, initial, "all nodes freed");
    memory_shutdown();
    return NULL;
}

TEST(vector_pop_to_empty) {
    memory_init();
    vector_init();
    size_t initial = memory_stats().objects_alive;

    /* 33826 elements: shift 15, crosses every collapse boundary on the way down */
    size_t n = 33826;
    Value keep = build_range(n);
    Value cur = vector_clone(keep);
    for (size_t len = n; len > 0; len--) {
        Value next = vector_pop_persistent(cur);
        object_release(cur);
        cur = next;
        ASSERT_EQ(vector_length(cur), len - 1, "pop shrinks by one");
        if (len > 1) {
            ASSERT_EQ(untag_fixnum(vector_last(cur)), (int64_t)len - 2, "new last element");
        }
    }
    ASSERT(is_nil(vector_pop_persistent(cur)), "pop of empty is nil");
    ASSERT(matches_range(keep, n), "persistent pops never touched the original");

    /* In-place pop returns an owned element */
    Value v = build_range(70);
    for (int64_t i = 69; i >= 0; i--) {
        Value x = vector_pop(v);
        ASSERT_EQ(untag_fixnum(x), i, "in-place pop order");
    }
    ASSERT(is_nil(vector_pop(v)), "in-place pop of empty is nil");

    object_release(v);
    object_release(cur);
    object_release(keep);
    ASSERT_EQ(memory_stats().objects_alive, initial, "all nodes freed");
    memory_shutdown();
    return NULL;
}

/* Heap elements: refcounts stay balanced through sharing and churn */
TEST(vector_heap_element_refcounts) {
    memory_init();
    vector_init();
    size_t initial = memory_stats().objects_alive;

    Value s = string_from_cstr("shared");
    Value v = vector_create(0);
    for (int i = 0; i < 2000; i++) vector_push(v, s);
    Value w = vector_assoc_n(v, 1500, make_fixnum(0));
    Value x = vector_pop_persistent(w);
    Value y = vector_conj(x, s);
    Value z = vector_clone(y);
    vector_push(z, s);

    object_release(v);
    object_release(w);
    object_release(x);
    object_release(y);
    object_release(z);
    ASSERT_EQ(object_refcount(s), 1, "element refcount back to the caller's single ref");
    object_release(s);
    ASSERT_EQ(memory_stats().objects_alive, initial, "everything freed");
    memory_shutdown();
    return NULL;
}

/* Random push/conj/assoc/pop/clone against a flat reference array. Every
 * 97 ops a snapshot (clone + reference copy) is kept; all snapshots must
 * still match at the end, which checks persistence under heavy sharing. */
#define FUZZ_MAX 40000
#define FUZZ_SNAPS 64

static uint64_t fuzz_state = 0x9E3779B97F4A7C15ULL;
static uint64_t fuzz_next(void) {
    fuzz_state ^= fuzz_state << 13;
    fuzz_state ^= fuzz_state >> 7;
    fuzz_state ^= fuzz_state << 17;
    return fuzz_state;
}

static bool matches_ref(Value v, const int64_t* ref, size_t n) {
    if (vector_length(v) != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (untag_fixnum(vector_get(v, i)) != ref[i]) return false;
    }
    return true;
}

TEST(vector_differential_fuzz) {
    memory_init();
    vector_init();
    size_t initial = memory_stats().objects_alive;

    static int64_t ref[FUZZ_MAX];
    size_t n = 0;
    Value cur = vector_create(0);

    static int64_t* snap_ref[FUZZ_SNAPS];
    size_t snap_len[FUZZ_SNAPS];
    Value snap_vec[FUZZ_SNAPS];
    int nsnaps = 0;

    for (int op = 0; op < 30000; op++) {
        uint64_t r = fuzz_next();
        int kind = (int)(r % 10);
        int64_t val = (int64_t)(r >> 20) % 1000000;

        if ((kind <= 3 && n < FUZZ_MAX) || n == 0) {
            vector_push(cur, make_fixnum(val));
            ref[n++] = val;
        } else if (kind == 4 && n < FUZZ_MAX) {
            Value next = vector_conj(cur, make_fixnum(val));
            object_release(cur);
            cur = next;
            ref[n++] = val;
        } else if (kind == 5) {
            size_t i = (size_t)(fuzz_next() % n);
            Value next = vector_assoc_n(cur, i, make_fixnum(val));
            object_release(cur);
            cur = next;
            ref[i] = val;
        } else if (kind == 6) {
            size_t i = (size_t)(fuzz_next() % n);
            vector_set(cur, i, make_fixnum(val));
            ref[i] = val;
        } else if (kind == 7) {
            Value next = vector_pop_persistent(cur);
            object_release(cur);
            cur = next;
            n--;
        } else if (kind == 8) {
            Value x = vector_pop(cur);
            ASSERT_EQ(untag_fixnum(x), ref[n - 1], "in-place pop returns last");
            n--;
        } else {
            /* Push a burst so the tree grows through boundaries quickly */
            for (int k = 0; k < 40 && n < FUZZ_MAX; k++) {
                vector_push(cur, make_fixnum(val + k));
                ref[n++] = val + k;
            }
        }

        if (op % 97 == 0 && nsnaps < FUZZ_SNAPS) {
            snap_vec[nsnaps] = vector_clone(cur);
            snap_ref[nsnaps] = malloc(n * sizeof(int64_t) + 1);
            memcpy(snap_ref[nsnaps], ref, n * sizeof(int64_t));
            snap_len[nsnaps] = n;
            nsnaps++;
        }
        if (op % 499 == 0) {
            ASSERT(matches_ref(cur, ref, n), "current vector matches reference");
        }
    }
    ASSERT(matches_ref(cur, ref, n), "final vector matches reference");
    for (int s = 0; s < nsnaps; s++) {
        ASSERT(matches_ref(snap_vec[s], snap_ref[s], snap_len[s]), "snapshot unchanged");
        object_release(snap_vec[s]);
        free(snap_ref[s]);
    }
    object_release(cur);
    ASSERT_EQ(memory_stats().objects_alive, initial, "fuzz freed everything");
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
    RUN_TEST(vector_trie_boundaries);
    RUN_TEST(vector_conj_persistence);
    RUN_TEST(vector_assoc_persistence);
    RUN_TEST(vector_topdown_sharing);
    RUN_TEST(vector_pop_to_empty);
    RUN_TEST(vector_heap_element_refcounts);
    RUN_TEST(vector_differential_fuzz);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing vector operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
