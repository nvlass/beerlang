/* Tests for bytecode disassembler */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "beerlang.h"

/* ANSI color codes */
#define ANSI_GREEN  "\x1b[32m"
#define ANSI_RED    "\x1b[31m"
#define ANSI_RESET  "\x1b[0m"

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Helper macros */
#define TEST(name) \
    printf("Testing: %s\n", name); \
    tests_run++

#define PASS() \
    do { \
        tests_passed++; \
        printf(ANSI_GREEN "  PASS: " ANSI_RESET "%s\n", __func__); \
    } while(0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        printf(ANSI_RED "  FAIL: %s\n" ANSI_RESET, msg); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
    } while(0)

#define ASSERT(cond) \
    if (!(cond)) { \
        FAIL("Assertion failed: " #cond); \
        return; \
    }

/* Test simple instructions */
void test_simple_instructions(void) {
    TEST("Simple instructions disassembly");

    uint8_t code[] = {
        OP_NOP,
        OP_PUSH_NIL,
        OP_PUSH_TRUE,
        OP_PUSH_FALSE,
        OP_ADD,
        OP_SUB,
        OP_MUL,
        OP_DIV,
        OP_NEG,
        OP_POP,
        OP_DUP,
        OP_SWAP,
        OP_RETURN,
        OP_HALT
    };

    printf("Disassembly:\n");
    disassemble_code(code, sizeof(code), "simple_instructions");

    PASS();
}

/* Test instructions with operands */
void test_instructions_with_operands(void) {
    TEST("Instructions with operands");

    uint8_t code[] = {
        OP_PUSH_INT, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* PUSH_INT 42 */
        OP_PUSH_CONST, 0x05, 0x00, 0x00, 0x00,                        /* PUSH_CONST 5 */
        OP_LOAD_LOCAL, 0x00, 0x00,                                    /* LOAD_LOCAL 0 */
        OP_STORE_LOCAL, 0x01, 0x00,                                   /* STORE_LOCAL 1 */
        OP_ENTER, 0x02, 0x00,                                         /* ENTER 2 */
        OP_CALL, 0x02, 0x00,                                          /* CALL 2 */
        OP_JUMP, 0x0A, 0x00, 0x00, 0x00,                              /* JUMP 10 */
        OP_JUMP_IF_FALSE, 0xF6, 0xFF, 0xFF, 0xFF,                     /* JUMP_IF_FALSE -10 */
        OP_HALT
    };

    printf("Disassembly:\n");
    disassemble_code(code, sizeof(code), "instructions_with_operands");

    PASS();
}

/* Test MAKE_CLOSURE instruction */
void test_make_closure(void) {
    TEST("MAKE_CLOSURE instruction");

    uint8_t code[] = {
        OP_MAKE_CLOSURE,
        0x64, 0x00, 0x00, 0x00,  /* code_offset = 100 */
        0x03, 0x00,               /* n_locals = 3 */
        0x02, 0x00,               /* n_closed = 2 */
        OP_HALT
    };

    printf("Disassembly:\n");
    disassemble_code(code, sizeof(code), "make_closure");

    PASS();
}

/* Test function bytecode (from test_function.c) */
void test_function_bytecode(void) {
    TEST("Function bytecode (add function)");

    /* Function: (fn [a b] (+ a b)) */
    uint8_t code[] = {
        /* Function at offset 0 */
        OP_ENTER, 0x00, 0x00,           /* 0: ENTER n_locals=0 */
        OP_LOAD_LOCAL, 0x00, 0x00,      /* 3: LOAD_LOCAL 0 (arg a) */
        OP_LOAD_LOCAL, 0x01, 0x00,      /* 6: LOAD_LOCAL 1 (arg b) */
        OP_ADD,                         /* 9: ADD */
        OP_RETURN,                      /* 10: RETURN */

        /* Main code starts at offset 11 */
        OP_PUSH_INT, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 11: PUSH_INT 10 */
        OP_PUSH_INT, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 20: PUSH_INT 32 */
        /* Function object would be pushed here via PUSH_CONST */
        OP_HALT                         /* 29: HALT */
    };

    printf("Disassembly:\n");
    disassemble_code(code, sizeof(code), "add_function");

    /* Manually verify key offsets */
    printf("\nKey offsets:\n");
    printf("  Function ENTER:  0x0000 (0)\n");
    printf("  Function RETURN: 0x000a (10)\n");
    printf("  Main starts:     0x000b (11)\n");
    printf("  First PUSH_INT:  0x000b (11)\n");
    printf("  Second PUSH_INT: 0x0014 (20)\n");
    printf("  HALT:            0x001d (29)\n");

    PASS();
}

/* Test incomplete instructions */
void test_incomplete_instructions(void) {
    TEST("Incomplete instructions (truncated bytecode)");

    /* PUSH_INT with incomplete operand */
    uint8_t code1[] = {
        OP_PUSH_INT, 0x42, 0x00  /* Missing 6 bytes */
    };

    printf("Disassembly of truncated PUSH_INT:\n");
    disassemble_code(code1, sizeof(code1), "truncated_push_int");

    /* ENTER with incomplete operand */
    uint8_t code2[] = {
        OP_ENTER, 0x02  /* Missing 1 byte */
    };

    printf("\nDisassembly of truncated ENTER:\n");
    disassemble_code(code2, sizeof(code2), "truncated_enter");

    PASS();
}

/* Test unknown opcode */
void test_unknown_opcode(void) {
    TEST("Unknown opcode");

    uint8_t code[] = {
        0xFF,  /* Invalid opcode */
        OP_HALT
    };

    printf("Disassembly:\n");
    disassemble_code(code, sizeof(code), "unknown_opcode");

    PASS();
}

int main(void) {
    printf("=== Disassembler Tests ===\n\n");

    test_simple_instructions();
    test_instructions_with_operands();
    test_make_closure();
    test_function_bytecode();
    test_incomplete_instructions();
    test_unknown_opcode();

    /* Summary */
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d, Passed: %d, Failed: %d\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
