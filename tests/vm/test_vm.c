/* Test VM operations */

#include <stdio.h>
#include "test.h"
#include "beerlang.h"
#include "vm.h"

/* Test VM creation and destruction */
TEST(vm_creation) {
    memory_init();

    VM* vm = vm_new(256);
    ASSERT(vm != NULL, "Should create VM");
    ASSERT(vm_stack_empty(vm), "Stack should be empty");
    ASSERT(!vm_stack_full(vm), "Stack should not be full");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test stack operations */
TEST(stack_operations) {
    memory_init();

    VM* vm = vm_new(10);

    /* Test push and pop */
    vm_push(vm, make_fixnum(42));
    ASSERT(!vm_stack_empty(vm), "Stack should not be empty");
    ASSERT_EQ(untag_fixnum(vm_peek(vm)), 42, "Peek should return 42");

    Value v = vm_pop(vm);
    ASSERT_EQ(untag_fixnum(v), 42, "Pop should return 42");
    ASSERT(vm_stack_empty(vm), "Stack should be empty");

    /* Test multiple values */
    vm_push(vm, make_fixnum(1));
    vm_push(vm, make_fixnum(2));
    vm_push(vm, make_fixnum(3));

    ASSERT_EQ(untag_fixnum(vm_peek(vm)), 3, "Peek should return 3");
    ASSERT_EQ(untag_fixnum(vm_pop(vm)), 3, "Pop should return 3");
    ASSERT_EQ(untag_fixnum(vm_pop(vm)), 2, "Pop should return 2");
    ASSERT_EQ(untag_fixnum(vm_pop(vm)), 1, "Pop should return 1");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_PUSH_NIL, OP_PUSH_TRUE, OP_PUSH_FALSE */
TEST(push_constants) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[] = {
        OP_PUSH_NIL,
        OP_PUSH_TRUE,
        OP_PUSH_FALSE,
        OP_HALT
    };

    vm_load_code(vm, code, sizeof(code));
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 3, "Should have 3 values on stack");

    Value v3 = vm_pop(vm);
    Value v2 = vm_pop(vm);
    Value v1 = vm_pop(vm);

    ASSERT(is_nil(v1), "First should be nil");
    ASSERT(is_true(v2), "Second should be true");
    ASSERT(is_false(v3), "Third should be false");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_PUSH_INT */
TEST(push_int) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 42 */
    code[pos++] = OP_PUSH_INT;
    int64_t value = 42;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (value >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT -100 */
    code[pos++] = OP_PUSH_INT;
    value = -100;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (value >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 2, "Should have 2 values on stack");

    Value v2 = vm_pop(vm);
    Value v1 = vm_pop(vm);

    ASSERT_EQ(untag_fixnum(v1), 42, "First should be 42");
    ASSERT_EQ(untag_fixnum(v2), -100, "Second should be -100");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_POP */
TEST(pop_instruction) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[] = {
        OP_PUSH_TRUE,
        OP_PUSH_FALSE,
        OP_POP,  /* Remove false */
        OP_HALT
    };

    vm_load_code(vm, code, sizeof(code));
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value v = vm_pop(vm);
    ASSERT(is_true(v), "Should be true");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_DUP */
TEST(dup_instruction) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[] = {
        OP_PUSH_TRUE,
        OP_DUP,
        OP_HALT
    };

    vm_load_code(vm, code, sizeof(code));
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 2, "Should have 2 values on stack");

    Value v2 = vm_pop(vm);
    Value v1 = vm_pop(vm);

    ASSERT(is_true(v1), "First should be true");
    ASSERT(is_true(v2), "Second should be true");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_SWAP */
TEST(swap_instruction) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[] = {
        OP_PUSH_TRUE,
        OP_PUSH_FALSE,
        OP_SWAP,
        OP_HALT
    };

    vm_load_code(vm, code, sizeof(code));
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 2, "Should have 2 values on stack");

    Value v2 = vm_pop(vm);
    Value v1 = vm_pop(vm);

    ASSERT(is_false(v1), "First should be false (was swapped)");
    ASSERT(is_true(v2), "Second should be true (was swapped)");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_ADD with fixnums */
TEST(add_fixnums) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 10 */
    code[pos++] = OP_PUSH_INT;
    int64_t val1 = 10;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val1 >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT 32 */
    code[pos++] = OP_PUSH_INT;
    int64_t val2 = 32;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val2 >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_ADD;
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "Result should be 42");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_SUB */
TEST(sub_fixnums) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 100 */
    code[pos++] = OP_PUSH_INT;
    int64_t val1 = 100;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val1 >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT 58 */
    code[pos++] = OP_PUSH_INT;
    int64_t val2 = 58;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val2 >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_SUB;
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "Result should be 42");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_MUL */
TEST(mul_fixnums) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 6 */
    code[pos++] = OP_PUSH_INT;
    int64_t val1 = 6;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val1 >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT 7 */
    code[pos++] = OP_PUSH_INT;
    int64_t val2 = 7;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val2 >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_MUL;
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "Result should be 42");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_DIV */
TEST(div_fixnums) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 84 */
    code[pos++] = OP_PUSH_INT;
    int64_t val1 = 84;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val1 >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT 2 */
    code[pos++] = OP_PUSH_INT;
    int64_t val2 = 2;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val2 >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_DIV;
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "Result should be 42");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test OP_NEG */
TEST(neg_fixnum) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 42 */
    code[pos++] = OP_PUSH_INT;
    int64_t val = 42;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_NEG;
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), -42, "Result should be -42");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test complex expression: 2 + 3 * 4 = 14 */
TEST(complex_expression) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 3 */
    code[pos++] = OP_PUSH_INT;
    int64_t val = 3;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT 4 */
    code[pos++] = OP_PUSH_INT;
    val = 4;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_MUL;  /* 3 * 4 = 12 */

    /* PUSH_INT 2 */
    code[pos++] = OP_PUSH_INT;
    val = 2;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_ADD;  /* 12 + 2 = 14 */
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 14, "Result should be 14");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test comparison: OP_EQ */
TEST(equality_comparison) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 42 */
    code[pos++] = OP_PUSH_INT;
    int64_t val = 42;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT 42 */
    code[pos++] = OP_PUSH_INT;
    val = 42;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_EQ;
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_true(result), "42 == 42 should be true");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test comparison: OP_LT */
TEST(less_than_comparison) {
    memory_init();

    VM* vm = vm_new(256);

    uint8_t code[100];
    int pos = 0;

    /* PUSH_INT 5 */
    code[pos++] = OP_PUSH_INT;
    int64_t val = 5;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    /* PUSH_INT 10 */
    code[pos++] = OP_PUSH_INT;
    val = 10;
    for (int i = 0; i < 8; i++) {
        code[pos++] = (val >> (i * 8)) & 0xFF;
    }

    code[pos++] = OP_LT;
    code[pos++] = OP_HALT;

    vm_load_code(vm, code, pos);
    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT_EQ(vm->stack_pointer, 1, "Should have 1 value on stack");

    Value result = vm_pop(vm);
    ASSERT(is_true(result), "5 < 10 should be true");

    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(vm_creation);
    RUN_TEST(stack_operations);
    RUN_TEST(push_constants);
    RUN_TEST(push_int);
    RUN_TEST(pop_instruction);
    RUN_TEST(dup_instruction);
    RUN_TEST(swap_instruction);
    RUN_TEST(add_fixnums);
    RUN_TEST(sub_fixnums);
    RUN_TEST(mul_fixnums);
    RUN_TEST(div_fixnums);
    RUN_TEST(neg_fixnum);
    RUN_TEST(complex_expression);
    RUN_TEST(equality_comparison);
    RUN_TEST(less_than_comparison);
    return NULL;
}

/* Main function */
int main(void) {
    printf("Testing VM operations...\n");
    RUN_SUITE(all_tests);
    return 0;
}
