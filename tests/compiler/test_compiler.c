/* Compiler Tests */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "beerlang.h"

/* Helper macros for type checking */
#define is_string(v) (is_pointer(v) && object_type(v) == TYPE_STRING)
#define is_symbol(v) (is_pointer(v) && object_type(v) == TYPE_SYMBOL)

/* Helper: compile and check for errors */
static CompiledCode* compile_string(const char* source) {
    memory_init();
    symbol_init();

    /* Read source - note that nil is a valid form! */
    Reader* r = reader_new(source, "<test>");
    Value form = reader_read(r);

    if (reader_has_error(r)) {
        fprintf(stderr, "Failed to read: %s\n", source);
        fprintf(stderr, "Error: %s\n", reader_error_msg(r));
        reader_free(r);
        memory_shutdown();
        return NULL;
    }
    reader_free(r);

    /* Compile */
    Compiler* c = compiler_new("<test>");
    CompiledCode* code = compile(c, form);

    if (compiler_has_error(c)) {
        fprintf(stderr, "Compilation error: %s\n", compiler_error_msg(c));
        fprintf(stderr, "Source: %s\n", source);
    }

    compiler_free(c);
    if (!is_nil(form)) {
        object_release(form);
    }

    return code;
}

/* =================================================================
 * Literal Compilation Tests
 * ================================================================= */

TEST(test_compile_nil) {
    CompiledCode* code = compile_string("nil");
    ASSERT(code != NULL, "Should compile nil");

    /* Check bytecode */
    ASSERT_EQ(code->bytecode[0], OP_PUSH_NIL, "Should emit PUSH_NIL");
    ASSERT_EQ(code->bytecode[1], OP_HALT, "Should emit HALT");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

TEST(test_compile_true) {
    CompiledCode* code = compile_string("true");
    ASSERT(code != NULL, "Should compile true");

    ASSERT_EQ(code->bytecode[0], OP_PUSH_TRUE, "Should emit PUSH_TRUE");
    ASSERT_EQ(code->bytecode[1], OP_HALT, "Should emit HALT");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

TEST(test_compile_false) {
    CompiledCode* code = compile_string("false");
    ASSERT(code != NULL, "Should compile false");

    ASSERT_EQ(code->bytecode[0], OP_PUSH_FALSE, "Should emit PUSH_FALSE");
    ASSERT_EQ(code->bytecode[1], OP_HALT, "Should emit HALT");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

TEST(test_compile_fixnum) {
    CompiledCode* code = compile_string("42");
    ASSERT(code != NULL, "Should compile fixnum");

    ASSERT_EQ(code->bytecode[0], OP_PUSH_INT, "Should emit PUSH_INT");
    /* Next 8 bytes are the int64_t value */
    int64_t value = 0;
    memcpy(&value, &code->bytecode[1], 8);
    ASSERT_EQ(value, 42, "Should push 42");
    ASSERT_EQ(code->bytecode[9], OP_HALT, "Should emit HALT");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

TEST(test_compile_string) {
    CompiledCode* code = compile_string("\"hello\"");
    ASSERT(code != NULL, "Should compile string");

    ASSERT_EQ(code->bytecode[0], OP_PUSH_CONST, "Should emit PUSH_CONST");
    ASSERT_EQ(code->bytecode[5], OP_HALT, "Should emit HALT");  /* 1 byte opcode + 4 bytes uint32 */

    /* Check constant pool */
    ASSERT_EQ(vector_length(code->constants), 1, "Should have 1 constant");
    Value const_val = vector_get(code->constants, 0);
    ASSERT(is_string(const_val), "Constant should be string");
    ASSERT(strcmp(string_cstr(const_val), "hello") == 0, "Should be 'hello'");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

TEST(test_compile_keyword) {
    CompiledCode* code = compile_string(":foo");
    ASSERT(code != NULL, "Should compile keyword");

    ASSERT_EQ(code->bytecode[0], OP_PUSH_CONST, "Should emit PUSH_CONST");

    /* Check constant pool */
    ASSERT_EQ(vector_length(code->constants), 1, "Should have 1 constant");
    Value const_val = vector_get(code->constants, 0);
    ASSERT(is_pointer(const_val) && object_type(const_val) == TYPE_KEYWORD,
           "Constant should be keyword");
    ASSERT(strcmp(keyword_name(const_val), "foo") == 0, "Should be :foo");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

/* =================================================================
 * Special Form Tests
 * ================================================================= */

TEST(test_compile_quote) {
    CompiledCode* code = compile_string("(quote foo)");
    ASSERT(code != NULL, "Should compile quote");

    /* (quote foo) should push foo as a literal */
    ASSERT_EQ(code->bytecode[0], OP_PUSH_CONST, "Should emit PUSH_CONST");

    /* Check constant pool has the symbol */
    ASSERT_EQ(vector_length(code->constants), 1, "Should have 1 constant");
    Value const_val = vector_get(code->constants, 0);
    ASSERT(is_symbol(const_val), "Constant should be symbol");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

TEST(test_compile_if) {
    CompiledCode* code = compile_string("(if true 1 2)");
    ASSERT(code != NULL, "Should compile if");

    /* Bytecode should have:
     * - PUSH_TRUE (test)
     * - JUMP_IF_FALSE (to else)
     * - PUSH_INT 1 (then)
     * - JUMP (to end)
     * - PUSH_INT 2 (else)
     * - HALT
     */
    ASSERT_EQ(code->bytecode[0], OP_PUSH_TRUE, "Should emit test");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

TEST(test_compile_do) {
    CompiledCode* code = compile_string("(do 1 2 3)");
    ASSERT(code != NULL, "Should compile do");

    /* Bytecode should have:
     * - PUSH_INT 1
     * - POP
     * - PUSH_INT 2
     * - POP
     * - PUSH_INT 3
     * - HALT
     */
    ASSERT_EQ(code->bytecode[0], OP_PUSH_INT, "Should emit first expr");

    compiled_code_free(code);
    memory_shutdown();
    return NULL;
}

/* =================================================================
 * Test Suite
 * ================================================================= */

static const char* all_tests(void) {
    /* Literal tests */
    RUN_TEST(test_compile_nil);
    RUN_TEST(test_compile_true);
    RUN_TEST(test_compile_false);
    RUN_TEST(test_compile_fixnum);
    RUN_TEST(test_compile_string);
    RUN_TEST(test_compile_keyword);

    /* Special form tests */
    RUN_TEST(test_compile_quote);
    RUN_TEST(test_compile_if);
    RUN_TEST(test_compile_do);

    return NULL;
}

int main(void) {
    printf("Testing compiler...\n\n");

    const char* result = all_tests();

    printf("\nall_tests\n");
    printf("==========================================\n");

    if (result) {
        printf("  FAIL: %s\n", result);
    } else {
        printf("\nALL TESTS PASSED\n");
    }

    printf("Tests run: %d\n", tests_run);

    return result != 0;
}
