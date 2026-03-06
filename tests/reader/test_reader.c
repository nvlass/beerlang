/* Tests for Reader */

#include <stdio.h>
#include <string.h>
#include "test.h"
#include "beerlang.h"

/* Helper macros for type checking */
#define is_string(v) (is_pointer(v) && object_type(v) == TYPE_STRING)
#define is_symbol(v) (is_pointer(v) && object_type(v) == TYPE_SYMBOL)
#define is_keyword(v) (is_pointer(v) && object_type(v) == TYPE_KEYWORD)

/* Test reading integers */
TEST(test_read_integers) {
    memory_init();

    Value v = read_string("42");
    ASSERT(is_fixnum(v), "Should read as fixnum");
    ASSERT_EQ(untag_fixnum(v), 42, "Should be 42");

    v = read_string("-17");
    ASSERT(is_fixnum(v), "Should read as fixnum");
    ASSERT_EQ(untag_fixnum(v), -17, "Should be -17");

    v = read_string("0xFF");
    ASSERT(is_fixnum(v), "Should read as fixnum");
    ASSERT_EQ(untag_fixnum(v), 255, "0xFF should be 255");

    memory_shutdown();
    return NULL;
}

/* Test reading special values */
TEST(test_read_special_values) {
    memory_init();

    Value v = read_string("nil");
    ASSERT(is_nil(v), "Should be nil");

    v = read_string("true");
    ASSERT(is_true(v), "Should be true");

    v = read_string("false");
    ASSERT(is_false(v), "Should be false");

    memory_shutdown();
    return NULL;
}

/* Test reading strings */
TEST(test_read_strings) {
    memory_init();

    Value v = read_string("\"hello\"");
    ASSERT(is_string(v), "Should be a string");
    ASSERT(strcmp(string_cstr(v), "hello") == 0, "Should be 'hello'");
    object_release(v);

    v = read_string("\"hello\\nworld\"");
    ASSERT(is_string(v), "Should be a string");
    ASSERT(strcmp(string_cstr(v), "hello\nworld") == 0, "Should have newline");
    object_release(v);

    memory_shutdown();
    return NULL;
}

/* Test reading characters */
TEST(test_read_characters) {
    memory_init();

    Value v = read_string("\\a");
    ASSERT(is_char(v), "Should be a character");
    ASSERT_EQ(untag_char(v), 'a', "Should be 'a'");

    v = read_string("\\newline");
    ASSERT(is_char(v), "Should be a character");
    ASSERT_EQ(untag_char(v), '\n', "Should be newline");

    v = read_string("\\space");
    ASSERT(is_char(v), "Should be a character");
    ASSERT_EQ(untag_char(v), ' ', "Should be space");

    memory_shutdown();
    return NULL;
}

/* Test reading symbols */
TEST(test_read_symbols) {
    memory_init();
    symbol_init();

    Value v = read_string("foo");
    ASSERT(is_symbol(v), "Should be a symbol");
    ASSERT(strcmp(symbol_name(v), "foo") == 0, "Should be 'foo'");

    v = read_string("foo/bar");
    ASSERT(is_symbol(v), "Should be a qualified symbol");
    ASSERT(strcmp(symbol_name(v), "bar") == 0, "Name should be 'bar'");
    ASSERT(strcmp(symbol_str(v), "foo/bar") == 0, "Full string should be 'foo/bar'");
    ASSERT(symbol_has_namespace(v), "Should have namespace");

    memory_shutdown();
    return NULL;
}

/* Test reading keywords */
TEST(test_read_keywords) {
    memory_init();
    symbol_init();

    Value v = read_string(":foo");
    ASSERT(is_keyword(v), "Should be a keyword");
    ASSERT(strcmp(keyword_name(v), "foo") == 0, "Should be ':foo'");

    v = read_string(":foo/bar");
    ASSERT(is_keyword(v), "Should be a qualified keyword");
    ASSERT(strcmp(keyword_name(v), "bar") == 0, "Name should be 'bar'");
    ASSERT(strcmp(keyword_str(v), "foo/bar") == 0, "Full string should be 'foo/bar'");
    ASSERT(keyword_has_namespace(v), "Should have namespace");

    memory_shutdown();
    return NULL;
}

/* Test reading lists */
TEST(test_read_lists) {
    memory_init();
    symbol_init();

    Value v = read_string("()");
    ASSERT(is_nil(v), "Empty list should be nil");

    v = read_string("(1 2 3)");
    ASSERT(is_cons(v), "Should be a list");
    ASSERT_EQ(untag_fixnum(car(v)), 1, "First element should be 1");
    ASSERT_EQ(untag_fixnum(car(cdr(v))), 2, "Second element should be 2");
    ASSERT_EQ(untag_fixnum(car(cdr(cdr(v)))), 3, "Third element should be 3");
    object_release(v);

    v = read_string("(+ 1 2)");
    ASSERT(is_cons(v), "Should be a list");
    ASSERT(is_symbol(car(v)), "First element should be a symbol");
    ASSERT(strcmp(symbol_name(car(v)), "+") == 0, "Should be '+");
    object_release(v);

    memory_shutdown();
    return NULL;
}

/* Test reading vectors */
TEST(test_read_vectors) {
    memory_init();

    Value v = read_string("[]");
    ASSERT(is_vector(v), "Should be a vector");
    ASSERT_EQ(vector_length(v), 0, "Should be empty");
    object_release(v);

    v = read_string("[1 2 3]");
    ASSERT(is_vector(v), "Should be a vector");
    ASSERT_EQ(vector_length(v), 3, "Should have 3 elements");
    ASSERT_EQ(untag_fixnum(vector_get(v, 0)), 1, "First element should be 1");
    ASSERT_EQ(untag_fixnum(vector_get(v, 1)), 2, "Second element should be 2");
    ASSERT_EQ(untag_fixnum(vector_get(v, 2)), 3, "Third element should be 3");
    object_release(v);

    memory_shutdown();
    return NULL;
}

/* Test reading maps */
TEST(test_read_maps) {
    memory_init();
    symbol_init();
    hashmap_init();

    Value v = read_string("{}");
    ASSERT(is_hashmap(v), "Should be a hashmap");
    ASSERT_EQ(hashmap_size(v), 0, "Should be empty");
    object_release(v);

    v = read_string("{:a 1 :b 2}");
    ASSERT(is_hashmap(v), "Should be a hashmap");
    ASSERT_EQ(hashmap_size(v), 2, "Should have 2 entries");

    Value key_a = keyword_intern("a");
    Value key_b = keyword_intern("b");
    Value val_a = hashmap_get(v, key_a);
    Value val_b = hashmap_get(v, key_b);

    ASSERT(is_fixnum(val_a), ":a should map to a number");
    ASSERT_EQ(untag_fixnum(val_a), 1, ":a should map to 1");
    ASSERT(is_fixnum(val_b), ":b should map to a number");
    ASSERT_EQ(untag_fixnum(val_b), 2, ":b should map to 2");

    object_release(v);
    memory_shutdown();
    return NULL;
}

/* Test reading quoted forms */
TEST(test_read_quote) {
    memory_init();
    symbol_init();

    Value v = read_string("'foo");
    ASSERT(is_cons(v), "Should be a list");
    ASSERT(is_symbol(car(v)), "First element should be 'quote' symbol");
    ASSERT(strcmp(symbol_name(car(v)), "quote") == 0, "Should be 'quote'");
    ASSERT(is_symbol(car(cdr(v))), "Second element should be the quoted symbol");
    ASSERT(strcmp(symbol_name(car(cdr(v))), "foo") == 0, "Should be 'foo'");
    object_release(v);

    v = read_string("'(1 2 3)");
    ASSERT(is_cons(v), "Should be a list");
    ASSERT(strcmp(symbol_name(car(v)), "quote") == 0, "Should start with 'quote'");
    Value quoted_list = car(cdr(v));
    ASSERT(is_cons(quoted_list), "Quoted form should be a list");
    object_release(v);

    memory_shutdown();
    return NULL;
}

/* Test reading with comments and whitespace */
TEST(test_read_comments_whitespace) {
    memory_init();

    Value v = read_string("  42  ");
    ASSERT(is_fixnum(v), "Should ignore whitespace");
    ASSERT_EQ(untag_fixnum(v), 42, "Should be 42");

    v = read_string("; comment\n42");
    ASSERT(is_fixnum(v), "Should ignore comments");
    ASSERT_EQ(untag_fixnum(v), 42, "Should be 42");

    v = read_string("(1 ; comment\n 2)");
    ASSERT(is_cons(v), "Should read list with comment");
    ASSERT_EQ(untag_fixnum(car(v)), 1, "First should be 1");
    ASSERT_EQ(untag_fixnum(car(cdr(v))), 2, "Second should be 2");
    object_release(v);

    /* Comma is whitespace in Clojure */
    v = read_string("(1, 2, 3)");
    ASSERT(is_cons(v), "Should read list with commas");
    object_release(v);

    memory_shutdown();
    return NULL;
}

/* Test error handling */
TEST(test_read_errors) {
    memory_init();

    Reader* r = reader_new("(1 2", "<test>");
    Value v = reader_read(r);
    (void)v; /* Unused */
    ASSERT(reader_has_error(r), "Should error on unclosed paren");
    reader_free(r);

    r = reader_new("{:a 1 :b}", "<test>");
    v = reader_read(r);
    (void)v;
    ASSERT(reader_has_error(r), "Should error on odd number of map forms");
    reader_free(r);

    r = reader_new(")", "<test>");
    v = reader_read(r);
    (void)v;
    ASSERT(reader_has_error(r), "Should error on unexpected closing paren");
    reader_free(r);

    memory_shutdown();
    return NULL;
}

/* Test reading from file */
TEST(test_read_from_file) {
    memory_init();
    symbol_init();

    /* Create a temporary file with test data */
    FILE* f = tmpfile();
    ASSERT(f != NULL, "Failed to create temporary file");

    const char* test_data = "(+ 1 2)\n[3 4 5]\n{:a 1 :b 2}";
    fprintf(f, "%s", test_data);
    rewind(f);  /* Rewind to beginning */

    /* Create reader from file */
    Reader* r = reader_new_file(f, "<temp>");
    ASSERT(r != NULL, "Failed to create reader from file");

    /* Read first form: (+ 1 2) */
    Value v1 = reader_read(r);
    ASSERT(is_cons(v1), "Should read a list");
    ASSERT(is_symbol(car(v1)), "First element should be symbol");
    ASSERT(strcmp(symbol_name(car(v1)), "+") == 0, "Should be '+'");
    object_release(v1);

    /* Read second form: [3 4 5] */
    Value v2 = reader_read(r);
    ASSERT(is_vector(v2), "Should read a vector");
    ASSERT_EQ(vector_length(v2), 3, "Vector should have 3 elements");
    object_release(v2);

    /* Read third form: {:a 1 :b 2} */
    Value v3 = reader_read(r);
    ASSERT(is_hashmap(v3), "Should read a hashmap");
    ASSERT_EQ(hashmap_size(v3), 2, "Map should have 2 entries");
    object_release(v3);

    /* Should be at EOF now */
    Value v4 = reader_read(r);
    ASSERT(is_nil(v4), "Should return nil at EOF");

    reader_free(r);  /* This also closes the file */
    memory_shutdown();
    return NULL;
}

/* Test reading multiple forms from file using read_all */
TEST(test_read_all_from_file) {
    memory_init();

    /* Create temporary file */
    FILE* f = tmpfile();
    ASSERT(f != NULL, "Failed to create temporary file");

    fprintf(f, "1 2 3 nil true false");
    rewind(f);

    /* Read all forms */
    Reader* r = reader_new_file(f, "<temp>");
    Value vec = reader_read_all(r);

    ASSERT(is_vector(vec), "read_all should return a vector");
    ASSERT_EQ(vector_length(vec), 6, "Should have 6 forms");

    ASSERT(is_fixnum(vector_get(vec, 0)), "First should be fixnum");
    ASSERT_EQ(untag_fixnum(vector_get(vec, 0)), 1, "First should be 1");

    ASSERT(is_nil(vector_get(vec, 3)), "Fourth should be nil");
    ASSERT(is_true(vector_get(vec, 4)), "Fifth should be true");
    ASSERT(is_false(vector_get(vec, 5)), "Sixth should be false");

    object_release(vec);
    reader_free(r);
    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests() {
    RUN_TEST(test_read_integers);
    RUN_TEST(test_read_special_values);
    RUN_TEST(test_read_strings);
    RUN_TEST(test_read_characters);
    RUN_TEST(test_read_symbols);
    RUN_TEST(test_read_keywords);
    RUN_TEST(test_read_lists);
    RUN_TEST(test_read_vectors);
    RUN_TEST(test_read_maps);
    RUN_TEST(test_read_quote);
    RUN_TEST(test_read_comments_whitespace);
    RUN_TEST(test_read_errors);
    RUN_TEST(test_read_from_file);
    RUN_TEST(test_read_all_from_file);
    return NULL;
}

int main(void) {
    RUN_SUITE(all_tests);
    return 0;
}
