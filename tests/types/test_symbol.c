/* Test symbol and keyword operations */

#include <stdio.h>
#include <string.h>
#include "test.h"
#include "beerlang.h"
#include "symbol.h"

/* Test symbol creation and interning */
TEST(symbol_interning) {
    memory_init();
    symbol_init();

    Value sym1 = symbol_intern("foo");
    Value sym2 = symbol_intern("foo");
    Value sym3 = symbol_intern("bar");

    ASSERT(is_pointer(sym1), "Symbol should be a pointer");
    ASSERT_EQ(object_type(sym1), TYPE_SYMBOL, "Type should be symbol");

    /* Same name should return same object (interned) */
    ASSERT(value_identical(sym1, sym2), "Interned symbols should be identical");
    ASSERT(!value_identical(sym1, sym3), "Different symbols should be different");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test symbol with namespace */
TEST(symbol_namespace) {
    memory_init();
    symbol_init();

    Value sym1 = symbol_intern_ns("clojure.core", "map");
    Value sym2 = symbol_intern("map");

    ASSERT(symbol_has_namespace(sym1), "Should have namespace");
    ASSERT(!symbol_has_namespace(sym2), "Should not have namespace");

    /* Verify namespace by checking full string */
    ASSERT_STR_EQ(symbol_str(sym1), "clojure.core/map", "Full string should include namespace");
    ASSERT_STR_EQ(symbol_str(sym2), "map", "Full string should be just name");

    /* Verify name extraction works correctly */
    ASSERT_STR_EQ(symbol_name(sym1), "map", "Name should be 'map'");
    ASSERT_STR_EQ(symbol_name(sym2), "map", "Name should be 'map'");

    /* Different namespaces should be different objects */
    Value sym3 = symbol_intern_ns("user", "map");
    ASSERT(!value_identical(sym1, sym3), "Different namespaces should be different");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test symbol equality */
TEST(symbol_equality) {
    memory_init();
    symbol_init();

    Value sym1 = symbol_intern("test");
    Value sym2 = symbol_intern("test");
    Value sym3 = symbol_intern("other");

    /* Pointer equality (fast) */
    ASSERT(symbol_eq(sym1, sym2), "Same symbols should be equal");
    ASSERT(!symbol_eq(sym1, sym3), "Different symbols should not be equal");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test keyword creation and interning */
TEST(keyword_interning) {
    memory_init();
    symbol_init();

    Value kw1 = keyword_intern("foo");
    Value kw2 = keyword_intern("foo");
    Value kw3 = keyword_intern("bar");

    ASSERT(is_pointer(kw1), "Keyword should be a pointer");
    ASSERT_EQ(object_type(kw1), TYPE_KEYWORD, "Type should be keyword");

    /* Same name should return same object (interned) */
    ASSERT(value_identical(kw1, kw2), "Interned keywords should be identical");
    ASSERT(!value_identical(kw1, kw3), "Different keywords should be different");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test keyword with namespace */
TEST(keyword_namespace) {
    memory_init();
    symbol_init();

    Value kw1 = keyword_intern_ns("user", "id");
    Value kw2 = keyword_intern("id");

    ASSERT(keyword_has_namespace(kw1), "Should have namespace");
    ASSERT(!keyword_has_namespace(kw2), "Should not have namespace");

    /* Verify namespace by checking full string */
    ASSERT_STR_EQ(keyword_str(kw1), "user/id", "Full string should include namespace");
    ASSERT_STR_EQ(keyword_str(kw2), "id", "Full string should be just name");

    /* Verify name extraction works correctly */
    ASSERT_STR_EQ(keyword_name(kw1), "id", "Name should be 'id'");
    ASSERT_STR_EQ(keyword_name(kw2), "id", "Name should be 'id'");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test keyword equality */
TEST(keyword_equality) {
    memory_init();
    symbol_init();

    Value kw1 = keyword_intern("test");
    Value kw2 = keyword_intern("test");
    Value kw3 = keyword_intern("other");

    /* Pointer equality (fast) */
    ASSERT(keyword_eq(kw1, kw2), "Same keywords should be equal");
    ASSERT(!keyword_eq(kw1, kw3), "Different keywords should not be equal");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test symbols and keywords are separate */
TEST(symbol_keyword_separation) {
    memory_init();
    symbol_init();

    Value sym = symbol_intern("foo");
    Value kw = keyword_intern("foo");

    /* Same name but different types = different objects */
    ASSERT(!value_identical(sym, kw), "Symbol and keyword with same name should be different");
    ASSERT_EQ(object_type(sym), TYPE_SYMBOL, "Should be symbol");
    ASSERT_EQ(object_type(kw), TYPE_KEYWORD, "Should be keyword");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test hash values */
TEST(symbol_keyword_hashing) {
    memory_init();
    symbol_init();

    Value sym1 = symbol_intern("test");
    Value sym2 = symbol_intern("test");
    Value sym3 = symbol_intern("different");

    uint32_t hash1 = symbol_hash(sym1);
    uint32_t hash2 = symbol_hash(sym2);
    uint32_t hash3 = symbol_hash(sym3);

    ASSERT_EQ(hash1, hash2, "Same symbols should have same hash");
    ASSERT(hash1 != hash3, "Different symbols should (likely) have different hash");

    Value kw1 = keyword_intern("key");
    Value kw2 = keyword_intern("key");

    ASSERT_EQ(keyword_hash(kw1), keyword_hash(kw2), "Same keywords should have same hash");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test interning statistics */
TEST(interning_statistics) {
    memory_init();
    symbol_init();

    InternStats stats = symbol_stats();
    size_t initial_symbols = stats.symbol_count;
    size_t initial_keywords = stats.keyword_count;

    symbol_intern("sym1");
    symbol_intern("sym2");
    keyword_intern("kw1");

    stats = symbol_stats();
    ASSERT_EQ(stats.symbol_count, initial_symbols + 2, "Should have 2 more symbols");
    ASSERT_EQ(stats.keyword_count, initial_keywords + 1, "Should have 1 more keyword");

    /* Re-interning same names should not increase count */
    symbol_intern("sym1");
    keyword_intern("kw1");

    stats = symbol_stats();
    ASSERT_EQ(stats.symbol_count, initial_symbols + 2, "Count should not increase");
    ASSERT_EQ(stats.keyword_count, initial_keywords + 1, "Count should not increase");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test memory management with interned values */
TEST(symbol_memory_management) {
    memory_init();
    symbol_init();

    MemoryStats initial = memory_stats();

    Value sym = symbol_intern("test-symbol");
    Value kw = keyword_intern("test-keyword");

    MemoryStats after_alloc = memory_stats();

    /* Interned values have refcount = 1 (held by interning table) */
    ASSERT_EQ(object_refcount(sym), 1, "Interned symbol should have refcount 1");
    ASSERT_EQ(object_refcount(kw), 1, "Interned keyword should have refcount 1");

    /* Objects are alive */
    ASSERT(after_alloc.objects_alive > initial.objects_alive, "Objects should be allocated");

    /* Note: Users should NOT release interned symbols/keywords - they live until shutdown */

    /* Shutdown releases interned values */
    symbol_shutdown();

    MemoryStats after_shutdown = memory_stats();
    ASSERT_EQ(after_shutdown.objects_alive, initial.objects_alive, "All objects should be freed");

    memory_shutdown();
    return NULL;
}

/* Test namespace interning */
TEST(namespace_interning) {
    memory_init();
    symbol_init();

    /* Same namespace/name should intern to same object */
    Value sym1 = symbol_intern_ns("ns", "name");
    Value sym2 = symbol_intern_ns("ns", "name");

    ASSERT(value_identical(sym1, sym2), "Same ns/name should be same object");

    /* Different namespace should be different */
    Value sym3 = symbol_intern_ns("other", "name");
    ASSERT(!value_identical(sym1, sym3), "Different namespace should be different object");

    /* Same name, no namespace vs with namespace should be different */
    Value sym4 = symbol_intern("name");
    ASSERT(!value_identical(sym1, sym4), "name != ns/name");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test edge cases */
TEST(symbol_edge_cases) {
    memory_init();
    symbol_init();

    /* Empty name (edge case, should work) */
    Value empty_sym = symbol_intern("");
    ASSERT(is_pointer(empty_sym), "Empty symbol should be created");
    ASSERT_STR_EQ(symbol_name(empty_sym), "", "Name should be empty");

    /* Long names */
    char long_name[256];
    memset(long_name, 'a', 255);
    long_name[255] = '\0';

    Value long_sym = symbol_intern(long_name);
    ASSERT(is_pointer(long_sym), "Long symbol should be created");
    ASSERT_STR_EQ(symbol_name(long_sym), long_name, "Long name should match");

    /* Special characters */
    Value special = symbol_intern("foo-bar?!");
    ASSERT(is_pointer(special), "Symbol with special chars should be created");
    ASSERT_STR_EQ(symbol_name(special), "foo-bar?!", "Special chars should be preserved");

    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(symbol_interning);
    RUN_TEST(symbol_namespace);
    RUN_TEST(symbol_equality);
    RUN_TEST(keyword_interning);
    RUN_TEST(keyword_namespace);
    RUN_TEST(keyword_equality);
    RUN_TEST(symbol_keyword_separation);
    RUN_TEST(symbol_keyword_hashing);
    RUN_TEST(interning_statistics);
    RUN_TEST(symbol_memory_management);
    RUN_TEST(namespace_interning);
    RUN_TEST(symbol_edge_cases);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing symbol and keyword operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
