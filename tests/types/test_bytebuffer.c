/* Test ByteBuffer type operations */

#include <string.h>
#include "test.h"
#include "beerlang.h"
#include "bytebuffer.h"

TEST(bytebuffer_alloc) {
    memory_init();

    Value b = bytebuffer_alloc(16);
    ASSERT(is_pointer(b), "ByteBuffer should be a pointer");
    ASSERT_EQ(object_type(b), TYPE_BYTEBUFFER, "Type should be bytebuffer");
    ASSERT_EQ(bytebuffer_capacity(b), 16, "Capacity should be 16");
    ASSERT_EQ(bytebuffer_position(b), 0, "Position should start at 0");
    ASSERT_EQ(bytebuffer_limit(b), 16, "Limit should start at capacity");

    /* calloc-backed — must be zeroed */
    uint8_t* data = bytebuffer_data(b);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(data[i], 0, "Fresh buffer should be zeroed");
    }

    object_release(b);
    memory_shutdown();
    return NULL;
}

TEST(bytebuffer_from_buffer) {
    memory_init();

    /* Bytes with a NUL and a high byte — would fail String's UTF-8 validation */
    const char raw[] = { 0x00, (char)0xFF, 0x42 };
    Value b = bytebuffer_from_buffer(raw, 3);
    ASSERT(is_pointer(b), "Should construct from raw bytes");
    ASSERT_EQ(bytebuffer_capacity(b), 3, "Capacity should match length");
    uint8_t* data = bytebuffer_data(b);
    ASSERT_EQ(data[0], 0x00, "byte 0");
    ASSERT_EQ(data[1], 0xFF, "byte 1");
    ASSERT_EQ(data[2], 0x42, "byte 2");

    object_release(b);
    memory_shutdown();
    return NULL;
}

TEST(bytebuffer_cursor_invariants) {
    memory_init();

    Value b = bytebuffer_alloc(10);
    bytebuffer_set_position(b, 4);
    ASSERT_EQ(bytebuffer_position(b), 4, "position set");

    bytebuffer_set_limit(b, 8);
    ASSERT_EQ(bytebuffer_limit(b), 8, "limit set");

    object_release(b);
    memory_shutdown();
    return NULL;
}

static const char* all_tests(void) {
    RUN_TEST(bytebuffer_alloc);
    RUN_TEST(bytebuffer_from_buffer);
    RUN_TEST(bytebuffer_cursor_invariants);
    return NULL;
}

int main(void) {
    printf("Testing bytebuffer operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
