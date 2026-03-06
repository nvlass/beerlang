/* Test string operations */

#include <stdio.h>
#include <string.h>
#include "test.h"
#include "beerlang.h"
#include "bstring.h"

/* Test string creation from C string */
TEST(string_from_cstr) {
    memory_init();

    Value str = string_from_cstr("hello");
    ASSERT(is_pointer(str), "String should be a pointer");
    ASSERT_EQ(object_type(str), TYPE_STRING, "Type should be string");
    ASSERT_EQ(string_byte_length(str), 5, "Byte length should be 5");
    ASSERT_EQ(string_char_length(str), 5, "Char length should be 5");
    ASSERT_STR_EQ(string_cstr(str), "hello", "Content should match");

    object_release(str);
    memory_shutdown();
    return NULL;
}

/* Test empty string */
TEST(string_empty) {
    memory_init();

    Value str = string_empty();
    ASSERT(is_pointer(str), "Empty string should be a pointer");
    ASSERT_EQ(string_byte_length(str), 0, "Byte length should be 0");
    ASSERT_EQ(string_char_length(str), 0, "Char length should be 0");
    ASSERT(string_is_empty(str), "Should be empty");
    ASSERT_STR_EQ(string_cstr(str), "", "Should be empty string");

    object_release(str);
    memory_shutdown();
    return NULL;
}

/* Test UTF-8 encoding */
TEST(string_utf8) {
    memory_init();

    /* UTF-8 string with multi-byte characters */
    const char* utf8_str = "Hello, 世界! λ";
    Value str = string_from_cstr(utf8_str);

    ASSERT(is_pointer(str), "String should be created");
    ASSERT_EQ(string_byte_length(str), strlen(utf8_str), "Byte length should match");
    /* "Hello, 世界! λ" = 7 ASCII + 2 Chinese (3 bytes each) + 2 spaces + 1 Greek (2 bytes) = 12 chars */
    ASSERT_EQ(string_char_length(str), 12, "Should have 12 UTF-8 characters");

    object_release(str);
    memory_shutdown();
    return NULL;
}

/* Test UTF-8 validation */
TEST(string_utf8_validation) {
    memory_init();

    /* Valid UTF-8 */
    Value valid = string_from_cstr("valid UTF-8");
    ASSERT(!is_nil(valid), "Valid UTF-8 should succeed");
    object_release(valid);

    /* Invalid UTF-8 (truncated multi-byte sequence) */
    const char invalid[] = {'h', 'e', 'l', 'l', 'o', (char)0xC0, '\0'};
    Value bad = string_from_buffer(invalid, 6);
    ASSERT(is_nil(bad), "Invalid UTF-8 should return nil");

    memory_shutdown();
    return NULL;
}

/* Test string concatenation */
TEST(string_concatenation) {
    memory_init();

    Value a = string_from_cstr("hello");
    Value b = string_from_cstr(" world");
    Value result = string_concat(a, b);

    ASSERT_STR_EQ(string_cstr(result), "hello world", "Concatenation should work");
    ASSERT_EQ(string_byte_length(result), 11, "Length should be 11");

    object_release(a);
    object_release(b);
    object_release(result);

    memory_shutdown();
    return NULL;
}

/* Test string comparison */
TEST(string_comparison) {
    memory_init();

    Value a = string_from_cstr("apple");
    Value b = string_from_cstr("banana");
    Value c = string_from_cstr("apple");

    ASSERT(string_cmp(a, b) < 0, "apple < banana");
    ASSERT(string_cmp(b, a) > 0, "banana > apple");
    ASSERT(string_cmp(a, c) == 0, "apple == apple");

    ASSERT(string_lt(a, b), "apple < banana");
    ASSERT(string_gt(b, a), "banana > apple");
    ASSERT(string_eq(a, c), "apple == apple");

    object_release(a);
    object_release(b);
    object_release(c);

    memory_shutdown();
    return NULL;
}

/* Test substring */
TEST(string_substring) {
    memory_init();

    Value str = string_from_cstr("hello world");
    Value sub = string_substring(str, 0, 5);

    ASSERT(!is_nil(sub), "Substring should succeed");
    ASSERT_STR_EQ(string_cstr(sub), "hello", "Substring should be 'hello'");

    object_release(sub);

    /* Extract "world" */
    sub = string_substring(str, 6, 11);
    ASSERT_STR_EQ(string_cstr(sub), "world", "Substring should be 'world'");
    object_release(sub);

    /* Invalid bounds */
    Value invalid = string_substring(str, 5, 3);
    ASSERT(is_nil(invalid), "Invalid bounds should return nil");

    object_release(str);
    memory_shutdown();
    return NULL;
}

/* Test character access */
TEST(string_char_at) {
    memory_init();

    Value str = string_from_cstr("abc");

    ASSERT_EQ(string_char_at(str, 0), 'a', "First char should be 'a'");
    ASSERT_EQ(string_char_at(str, 1), 'b', "Second char should be 'b'");
    ASSERT_EQ(string_char_at(str, 2), 'c', "Third char should be 'c'");
    ASSERT_EQ(string_char_at(str, 3), 0, "Out of bounds should return 0");

    object_release(str);

    /* UTF-8 multi-byte character */
    str = string_from_cstr("λ");  /* Greek lambda, U+03BB, encoded as 0xCE 0xBB */
    ASSERT_EQ(string_char_at(str, 0), 0x03BB, "Should decode lambda correctly");

    object_release(str);
    memory_shutdown();
    return NULL;
}

/* Test starts_with */
TEST(string_starts_with) {
    memory_init();

    Value str = string_from_cstr("hello world");
    Value prefix1 = string_from_cstr("hello");
    Value prefix2 = string_from_cstr("world");

    ASSERT(string_starts_with(str, prefix1), "Should start with 'hello'");
    ASSERT(!string_starts_with(str, prefix2), "Should not start with 'world'");

    object_release(str);
    object_release(prefix1);
    object_release(prefix2);

    memory_shutdown();
    return NULL;
}

/* Test ends_with */
TEST(string_ends_with) {
    memory_init();

    Value str = string_from_cstr("hello world");
    Value suffix1 = string_from_cstr("world");
    Value suffix2 = string_from_cstr("hello");

    ASSERT(string_ends_with(str, suffix1), "Should end with 'world'");
    ASSERT(!string_ends_with(str, suffix2), "Should not end with 'hello'");

    object_release(str);
    object_release(suffix1);
    object_release(suffix2);

    memory_shutdown();
    return NULL;
}

/* Test contains */
TEST(string_contains) {
    memory_init();

    Value str = string_from_cstr("hello world");
    Value sub1 = string_from_cstr("lo wo");
    Value sub2 = string_from_cstr("xyz");

    ASSERT(string_contains(str, sub1), "Should contain 'lo wo'");
    ASSERT(!string_contains(str, sub2), "Should not contain 'xyz'");

    object_release(str);
    object_release(sub1);
    object_release(sub2);

    memory_shutdown();
    return NULL;
}

/* Test index_of */
TEST(string_index_of) {
    memory_init();

    Value str = string_from_cstr("hello world");
    Value sub1 = string_from_cstr("world");
    Value sub2 = string_from_cstr("xyz");

    ASSERT_EQ(string_index_of(str, sub1), 6, "Should find 'world' at index 6");
    ASSERT_EQ(string_index_of(str, sub2), -1, "Should not find 'xyz'");

    object_release(str);
    object_release(sub1);
    object_release(sub2);

    memory_shutdown();
    return NULL;
}

/* Test hash computation */
TEST(string_hash) {
    memory_init();

    Value str1 = string_from_cstr("test");
    Value str2 = string_from_cstr("test");
    Value str3 = string_from_cstr("different");

    uint32_t hash1 = string_hash(str1);
    uint32_t hash2 = string_hash(str2);
    uint32_t hash3 = string_hash(str3);

    ASSERT_EQ(hash1, hash2, "Same strings should have same hash");
    ASSERT(hash1 != hash3, "Different strings should (likely) have different hash");

    /* Hash should be cached */
    ASSERT_EQ(string_hash(str1), hash1, "Cached hash should match");

    object_release(str1);
    object_release(str2);
    object_release(str3);

    memory_shutdown();
    return NULL;
}

/* Test UTF-8 utilities */
TEST(utf8_utilities) {
    memory_init();

    /* Test utf8_char_length */
    ASSERT_EQ(utf8_char_length('a'), 1, "ASCII should be 1 byte");
    ASSERT_EQ(utf8_char_length(0xC0), 2, "2-byte sequence");
    ASSERT_EQ(utf8_char_length(0xE0), 3, "3-byte sequence");
    ASSERT_EQ(utf8_char_length(0xF0), 4, "4-byte sequence");

    /* Test utf8_encode */
    char buf[5];
    ASSERT_EQ(utf8_encode('A', buf), 1, "ASCII encoding");
    ASSERT_EQ(buf[0], 'A', "Should encode correctly");

    ASSERT_EQ(utf8_encode(0x03BB, buf), 2, "Greek lambda encoding");
    ASSERT_EQ((unsigned char)buf[0], 0xCE, "First byte of lambda");
    ASSERT_EQ((unsigned char)buf[1], 0xBB, "Second byte of lambda");

    /* Test utf8_count_chars */
    const char* utf8_str = "Hello λ";
    ASSERT_EQ(utf8_count_chars(utf8_str, strlen(utf8_str)), 7, "Should count 7 characters");

    memory_shutdown();
    return NULL;
}

/* Test memory management */
TEST(string_memory_management) {
    memory_init();

    MemoryStats initial = memory_stats();

    Value str = string_from_cstr("test string");
    MemoryStats after_alloc = memory_stats();

    ASSERT_EQ(after_alloc.objects_alive - initial.objects_alive, 1,
              "One object allocated");

    object_release(str);
    MemoryStats after_free = memory_stats();

    ASSERT_EQ(after_free.objects_alive, initial.objects_alive,
              "Object freed");

    memory_shutdown();
    return NULL;
}

/* Test immutability (strings are immutable, operations create new strings) */
TEST(string_immutability) {
    memory_init();

    Value original = string_from_cstr("original");
    const char* original_cstr = string_cstr(original);

    /* Concatenation creates new string, doesn't modify original */
    Value suffix = string_from_cstr(" text");
    Value concatenated = string_concat(original, suffix);

    ASSERT_STR_EQ(string_cstr(original), "original", "Original unchanged");
    ASSERT(string_cstr(original) == original_cstr, "Original pointer unchanged");
    ASSERT_STR_EQ(string_cstr(concatenated), "original text", "New string created");

    object_release(original);
    object_release(suffix);
    object_release(concatenated);

    memory_shutdown();
    return NULL;
}

/* Test edge cases */
TEST(string_edge_cases) {
    memory_init();

    /* Empty string operations */
    Value empty = string_empty();
    Value hello = string_from_cstr("hello");

    Value concat = string_concat(empty, hello);
    ASSERT_STR_EQ(string_cstr(concat), "hello", "Empty + hello = hello");
    object_release(concat);

    concat = string_concat(hello, empty);
    ASSERT_STR_EQ(string_cstr(concat), "hello", "hello + empty = hello");
    object_release(concat);

    /* Substring of entire string */
    Value sub = string_substring(hello, 0, string_byte_length(hello));
    ASSERT_STR_EQ(string_cstr(sub), "hello", "Full substring should match");
    object_release(sub);

    /* Empty substring */
    sub = string_substring(hello, 2, 2);
    ASSERT_EQ(string_byte_length(sub), 0, "Empty substring");
    object_release(sub);

    object_release(empty);
    object_release(hello);

    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(string_from_cstr);
    RUN_TEST(string_empty);
    RUN_TEST(string_utf8);
    RUN_TEST(string_utf8_validation);
    RUN_TEST(string_concatenation);
    RUN_TEST(string_comparison);
    RUN_TEST(string_substring);
    RUN_TEST(string_char_at);
    RUN_TEST(string_starts_with);
    RUN_TEST(string_ends_with);
    RUN_TEST(string_contains);
    RUN_TEST(string_index_of);
    RUN_TEST(string_hash);
    RUN_TEST(utf8_utilities);
    RUN_TEST(string_memory_management);
    RUN_TEST(string_immutability);
    RUN_TEST(string_edge_cases);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing string operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
