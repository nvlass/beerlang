/* Test memory management - allocator and reference counting */

#include <stdio.h>
#include "test.h"
#include "beerlang.h"
#include "memory.h"

/* Test object allocation */
TEST(object_allocation) {
    memory_init();

    /* Allocate a simple object */
    struct Object* raw = object_alloc(TYPE_STRING, sizeof(struct Object));
    Value obj = tag_pointer(raw);

    ASSERT(is_pointer(obj), "Allocated value should be a pointer");
    ASSERT_EQ(object_type(obj), TYPE_STRING, "Object type should match");
    ASSERT_EQ(object_refcount(obj), 1, "Initial refcount should be 1");

    /* Check alignment (pointer should be 8-byte aligned) */
    ASSERT(((uintptr_t)raw & 0x7) == 0, "Object should be 8-byte aligned");

    object_release(obj);
    memory_shutdown();
    return NULL;
}

/* Test reference counting */
TEST(reference_counting) {
    memory_init();

    Value obj = tag_pointer(object_alloc(TYPE_VECTOR, sizeof(struct Object)));
    ASSERT_EQ(object_refcount(obj), 1, "Initial refcount = 1");

    /* Retain increases refcount */
    object_retain(obj);
    ASSERT_EQ(object_refcount(obj), 2, "After retain, refcount = 2");

    object_retain(obj);
    ASSERT_EQ(object_refcount(obj), 3, "After second retain, refcount = 3");

    /* Release decreases refcount */
    object_release(obj);
    ASSERT_EQ(object_refcount(obj), 2, "After release, refcount = 2");

    object_release(obj);
    ASSERT_EQ(object_refcount(obj), 1, "After second release, refcount = 1");

    /* Final release frees the object */
    object_release(obj);

    memory_shutdown();
    return NULL;
}

/* Test immediate values don't interfere with refcounting */
TEST(immediate_values) {
    memory_init();

    /* These should all be no-ops */
    Value nil_val = VALUE_NIL;
    Value true_val = VALUE_TRUE;
    Value false_val = VALUE_FALSE;
    Value fixnum_val = make_fixnum(42);

    /* Retain/release on immediates should be safe */
    object_retain(nil_val);
    object_release(nil_val);
    object_retain(true_val);
    object_release(true_val);
    object_retain(false_val);
    object_release(false_val);
    object_retain(fixnum_val);
    object_release(fixnum_val);

    /* Refcount should be 0 for non-pointers */
    ASSERT_EQ(object_refcount(nil_val), 0, "Nil refcount = 0");
    ASSERT_EQ(object_refcount(fixnum_val), 0, "Fixnum refcount = 0");

    memory_shutdown();
    return NULL;
}

/* Test memory statistics */
TEST(memory_statistics) {
    memory_init();

    MemoryStats stats = memory_stats();
    ASSERT_EQ(stats.objects_alive, 0, "Initially no objects alive");
    ASSERT_EQ(stats.current_allocated, 0, "Initially nothing allocated");

    /* Allocate first object */
    Value obj1 = tag_pointer(object_alloc(TYPE_CONS, sizeof(struct Object)));
    stats = memory_stats();
    ASSERT_EQ(stats.objects_alive, 1, "One object alive");
    ASSERT_EQ(stats.allocation_count, 1, "One allocation");
    ASSERT(stats.current_allocated >= sizeof(struct Object), "Memory allocated");

    /* Allocate second object */
    Value obj2 = tag_pointer(object_alloc(TYPE_HASHMAP, sizeof(struct Object) + 64));
    stats = memory_stats();
    ASSERT_EQ(stats.objects_alive, 2, "Two objects alive");
    ASSERT_EQ(stats.allocation_count, 2, "Two allocations");

    /* Release first object */
    object_release(obj1);
    stats = memory_stats();
    ASSERT_EQ(stats.objects_alive, 1, "One object alive after release");
    ASSERT_EQ(stats.free_count, 1, "One object freed");

    /* Release second object */
    object_release(obj2);
    stats = memory_stats();
    ASSERT_EQ(stats.objects_alive, 0, "No objects alive");
    ASSERT_EQ(stats.free_count, 2, "Two objects freed");
    ASSERT_EQ(stats.current_allocated, 0, "All memory freed");

    memory_shutdown();
    return NULL;
}

/* Test destructor registration and calling */
static int destructor_called = 0;
static uint8_t destructor_type = 0;

static void test_destructor(struct Object* obj) {
    destructor_called++;
    destructor_type = obj->type & 0xFF;
}

TEST(destructor_callback) {
    memory_init();
    destructor_called = 0;
    destructor_type = 0;

    /* Register destructor for TYPE_FUNCTION */
    object_register_destructor(TYPE_FUNCTION, test_destructor);

    /* Allocate and release an object of that type */
    Value obj = tag_pointer(object_alloc(TYPE_FUNCTION, sizeof(struct Object)));
    ASSERT_EQ(destructor_called, 0, "Destructor not called yet");

    object_release(obj);
    ASSERT_EQ(destructor_called, 1, "Destructor called once");
    ASSERT_EQ(destructor_type, TYPE_FUNCTION, "Correct type passed to destructor");

    memory_shutdown();
    return NULL;
}

/* Test that destructor is only called when refcount reaches zero */
TEST(destructor_only_on_zero) {
    memory_init();
    destructor_called = 0;

    object_register_destructor(TYPE_VAR, test_destructor);

    Value obj = tag_pointer(object_alloc(TYPE_VAR, sizeof(struct Object)));
    object_retain(obj);  /* refcount = 2 */

    /* First release should not call destructor */
    object_release(obj);
    ASSERT_EQ(destructor_called, 0, "Destructor not called (refcount still > 0)");

    /* Second release should call destructor */
    object_release(obj);
    ASSERT_EQ(destructor_called, 1, "Destructor called when refcount = 0");

    memory_shutdown();
    return NULL;
}

/* Test multiple objects with different types */
TEST(multiple_object_types) {
    memory_init();

    Value s = tag_pointer(object_alloc(TYPE_STRING, sizeof(struct Object)));
    Value v = tag_pointer(object_alloc(TYPE_VECTOR, sizeof(struct Object)));
    Value h = tag_pointer(object_alloc(TYPE_HASHMAP, sizeof(struct Object)));
    Value c = tag_pointer(object_alloc(TYPE_CONS, sizeof(struct Object)));

    ASSERT_EQ(object_type(s), TYPE_STRING, "String type");
    ASSERT_EQ(object_type(v), TYPE_VECTOR, "Vector type");
    ASSERT_EQ(object_type(h), TYPE_HASHMAP, "HashMap type");
    ASSERT_EQ(object_type(c), TYPE_CONS, "Cons type");

    MemoryStats stats = memory_stats();
    ASSERT_EQ(stats.objects_alive, 4, "Four objects alive");

    object_release(s);
    object_release(v);
    object_release(h);
    object_release(c);

    stats = memory_stats();
    ASSERT_EQ(stats.objects_alive, 0, "All objects freed");

    memory_shutdown();
    return NULL;
}

/* Test direct memory allocation */
TEST(direct_memory_allocation) {
    memory_init();

    void* buf1 = mem_alloc(128);
    ASSERT(buf1 != NULL, "mem_alloc should succeed");

    void* buf2 = mem_realloc(buf1, 128, 256);
    ASSERT(buf2 != NULL, "mem_realloc should succeed");

    mem_free(buf2, 256);

    memory_shutdown();
    return NULL;
}

/* Test large object allocation */
TEST(large_object_allocation) {
    memory_init();

    /* Allocate object larger than Object header */
    size_t large_size = sizeof(struct Object) + 1024;
    struct Object* ptr = object_alloc(TYPE_BIGINT, large_size);
    Value obj = tag_pointer(ptr);

    ASSERT_EQ(ptr->size, large_size, "Size should be stored correctly");

    object_release(obj);

    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(object_allocation);
    RUN_TEST(reference_counting);
    RUN_TEST(immediate_values);
    RUN_TEST(memory_statistics);
    RUN_TEST(destructor_callback);
    RUN_TEST(destructor_only_on_zero);
    RUN_TEST(multiple_object_types);
    RUN_TEST(direct_memory_allocation);
    RUN_TEST(large_object_allocation);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing memory management...\n");
    RUN_SUITE(all_tests);
    return 0;
}
