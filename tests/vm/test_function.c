/* Tests for function calls, closures, and tail calls */

#include <stdio.h>
#include <stdlib.h>
#include "test.h"
#include "beerlang.h"

/* Test creating a function object */
TEST(test_function_create) {
    memory_init();

    /* Create a simple function */
    Value fn = function_new(2, 100, 0, "test-fn");  /* arity=2, code_offset=100, n_locals=0 */

    ASSERT(is_function(fn), "Should be a function");
    ASSERT_EQ(function_arity(fn), 2, "Arity should be 2");
    ASSERT_EQ(function_code_offset(fn), 100, "Code offset should be 100");
    ASSERT_EQ(function_n_locals(fn), 0, "Should have 0 locals");
    ASSERT_EQ(function_n_closed(fn), 0, "Should have 0 closed variables");

    object_release(fn);
    memory_shutdown();
    return NULL;
}

/* Test creating a closure */
TEST(test_closure_create) {
    memory_init();

    /* Create some values to capture */
    Value captured[2];
    captured[0] = make_fixnum(42);
    captured[1] = make_fixnum(99);

    /* Create a closure */
    Value closure = function_new_closure(1, 200, 1, 2, captured, "test-closure");

    ASSERT(is_function(closure), "Should be a function");
    ASSERT_EQ(function_arity(closure), 1, "Arity should be 1");
    ASSERT_EQ(function_code_offset(closure), 200, "Code offset should be 200");
    ASSERT_EQ(function_n_locals(closure), 1, "Should have 1 local");
    ASSERT_EQ(function_n_closed(closure), 2, "Should have 2 closed variables");

    /* Check captured values */
    Value closed0 = function_get_closed(closure, 0);
    Value closed1 = function_get_closed(closure, 1);
    ASSERT(is_fixnum(closed0), "First captured should be fixnum");
    ASSERT(is_fixnum(closed1), "Second captured should be fixnum");
    ASSERT_EQ(untag_fixnum(closed0), 42, "First captured should be 42");
    ASSERT_EQ(untag_fixnum(closed1), 99, "Second captured should be 99");

    object_release(closure);
    memory_shutdown();
    return NULL;
}

/* Test simple function call: () -> 42 */
TEST(test_simple_function_call) {
    memory_init();

    VM* vm = vm_new(256);

    /*
     * Function at offset 0:
     * ENTER 0          ; no locals
     * PUSH_INT 42
     * RETURN
     *
     * Main:
     * PUSH_CONST 0     ; push function
     * CALL 0           ; call with 0 args
     * HALT
     */

    /* Create function object */
    Value fn = function_new(0, 0, 0, "test-fn");  /* arity=0, starts at offset 0 */

    /* Constant pool */
    Value constants[] = { fn };
    vm_load_constants(vm, constants, 1);

    /* Bytecode */
    uint8_t code[] = {
        /* Function body (offset 0): */
        OP_ENTER, 0, 0,           /* ENTER 0 locals */
        OP_PUSH_INT, 42, 0, 0, 0, 0, 0, 0, 0,  /* PUSH_INT 42 */
        OP_RETURN,                /* RETURN */

        /* Main (offset 13): */
        OP_PUSH_CONST, 0, 0, 0, 0,  /* PUSH_CONST 0 */
        OP_CALL, 0, 0,            /* CALL 0 */
        OP_HALT                   /* HALT */
    };

    vm_load_code(vm, code, sizeof(code));

    /* Disassemble for debugging */
    printf("\nBytecode disassembly:\n");
    disassemble_code(code, sizeof(code), "test_simple_function_call");
    printf("\n");

    /* Start at main (offset 13) */
    vm->pc = 13;
    vm_run(vm);

    ASSERT(!vm->error, "Should not error");
    ASSERT(!vm_stack_empty(vm), "Result should be on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "Result should be 42");

    object_release(fn);
    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test function with arguments: (a, b) -> a + b */
TEST(test_function_with_args) {
    memory_init();

    VM* vm = vm_new(256);

    /*
     * Function: add(a, b) -> a + b
     * Arguments are on stack before call
     * Stack layout after CALL: [... prev_stack | arg0 | arg1 | locals...]
     *
     * ENTER 0          ; no locals beyond args
     * LOAD_LOCAL 0     ; load arg a
     * LOAD_LOCAL 1     ; load arg b
     * ADD
     * RETURN
     */

    Value fn = function_new(2, 0, 0, "test-fn");  /* arity=2 */

    Value constants[] = { fn };
    vm_load_constants(vm, constants, 1);

    uint8_t code[] = {
        /* Function body (offset 0): */
        OP_ENTER, 0, 0,           /* ENTER 0 locals */
        OP_LOAD_LOCAL, 0, 0,      /* LOAD_LOCAL 0 (arg a) */
        OP_LOAD_LOCAL, 1, 0,      /* LOAD_LOCAL 1 (arg b) */
        OP_ADD,                   /* ADD */
        OP_RETURN,                /* RETURN */

        /* Main (offset 11): */
        OP_PUSH_INT, 10, 0, 0, 0, 0, 0, 0, 0,  /* arg a = 10 */
        OP_PUSH_INT, 32, 0, 0, 0, 0, 0, 0, 0,  /* arg b = 32 */
        OP_PUSH_CONST, 0, 0, 0, 0,  /* function */
        OP_CALL, 2, 0,            /* CALL 2 args */
        OP_HALT
    };

    vm_load_code(vm, code, sizeof(code));

    /* Disassemble for debugging */
    printf("\nBytecode disassembly:\n");
    disassemble_code(code, sizeof(code), "test_function_with_args");
    printf("\n");

    vm->pc = 11;  /* Start at main */
    vm_run(vm);

    ASSERT(!vm->error, "Should not error");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "10 + 32 = 42");

    object_release(fn);
    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test function with local variables */
TEST(test_function_with_locals) {
    log_set_level(ULOG_LEVEL_TRACE);
    memory_init();

    VM* vm = vm_new(256);

    /*
     * Function: square_sum(a, b) -> (a*a) + (b*b)
     * Uses locals to store intermediate results
     *
     * ENTER 2          ; 2 locals (beyond 2 args)
     * LOAD_LOCAL 0     ; a
     * LOAD_LOCAL 0     ; a
     * MUL              ; a*a
     * STORE_LOCAL 2    ; local[0] = a*a
     * LOAD_LOCAL 1     ; b
     * LOAD_LOCAL 1     ; b
     * MUL              ; b*b
     * STORE_LOCAL 3    ; local[1] = b*b
     * LOAD_LOCAL 2     ; load local[0]
     * LOAD_LOCAL 3     ; load local[1]
     * ADD              ; sum them
     * RETURN
     */

    Value fn = function_new(2, 0, 2, "square-sum");  /* arity=2, n_locals=2 */

    Value constants[] = { fn };
    vm_load_constants(vm, constants, 1);

    uint8_t code[] = {
        /* Function body (offset 0): */
        OP_ENTER, 2, 0,           /* ENTER 2 locals */
        OP_LOAD_LOCAL, 0, 0,      /* a */
        OP_LOAD_LOCAL, 0, 0,      /* a */
        OP_MUL,                   /* a*a */
        OP_STORE_LOCAL, 2, 0,     /* store to local[0] (slot 2) */
        OP_LOAD_LOCAL, 1, 0,      /* b */
        OP_LOAD_LOCAL, 1, 0,      /* b */
        OP_MUL,                   /* b*b */
        OP_STORE_LOCAL, 3, 0,     /* store to local[1] (slot 3) */
        OP_LOAD_LOCAL, 2, 0,      /* load local[0] */
        OP_LOAD_LOCAL, 3, 0,      /* load local[1] */
        OP_ADD,                   /* sum */
        OP_RETURN,

        /* Main (offset 26): */
        OP_PUSH_INT, 3, 0, 0, 0, 0, 0, 0, 0,   /* a = 3 */
        OP_PUSH_INT, 4, 0, 0, 0, 0, 0, 0, 0,   /* b = 4 */
        OP_PUSH_CONST, 0, 0, 0, 0,  /* function */
        OP_CALL, 2, 0,            /* CALL 2 args */
        OP_HALT
    };

    LOG_INFO("CODE SIZE: %d", sizeof(code));
    vm_load_code(vm, code, sizeof(code));

    /* Disassemble for debugging */
    printf("\nBytecode disassembly:\n");
    disassemble_code(code, sizeof(code), "test_function_with_locals");
    printf("\n");

    vm->pc = 0x1f;  /* Start at main (offset 31 - see disassembly) */
    vm_run(vm);

    ASSERT(!vm->error, "Should not error");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 25, "3*3 + 4*4 = 9 + 16 = 25");

    object_release(fn);
    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test recursive function: factorial(n) */
TEST(test_recursive_factorial) {
    memory_init();

    VM* vm = vm_new(256);

    /*
     * factorial(n):
     *   if n <= 1: return 1
     *   else: return n * factorial(n-1)
     *
     * ENTER 0
     * LOAD_LOCAL 0         ; n
     * PUSH_INT 1
     * GT                   ; n > 1?
     * JUMP_IF_FALSE +label ; if not, return 1
     * ; recursive case:
     * LOAD_LOCAL 0         ; n
     * LOAD_LOCAL 0         ; n
     * PUSH_INT 1
     * SUB                  ; n - 1
     * PUSH_CONST 0         ; factorial function
     * CALL 1               ; factorial(n-1)
     * MUL                  ; n * result
     * RETURN
     * label:
     * PUSH_INT 1           ; base case
     * RETURN
     */

    Value fn = function_new(1, 0, 0, "factorial");  /* arity=1 */

    Value constants[] = { fn };
    vm_load_constants(vm, constants, 1);

    uint8_t code[] = {
        /* Function body (offset 0): */
        OP_ENTER, 0, 0,           /* ENTER 0 locals */
        OP_LOAD_LOCAL, 0, 0,      /* n */
        OP_PUSH_INT, 1, 0, 0, 0, 0, 0, 0, 0,  /* 1 */
        OP_GT,                    /* n > 1 */
        OP_JUMP_IF_FALSE, 26, 0, 0, 0,  /* skip to base case (offset +26 to reach 0x2f) */
        /* recursive case: */
        OP_LOAD_LOCAL, 0, 0,      /* n */
        OP_LOAD_LOCAL, 0, 0,      /* n */
        OP_PUSH_INT, 1, 0, 0, 0, 0, 0, 0, 0,  /* 1 */
        OP_SUB,                   /* n - 1 */
        OP_PUSH_CONST, 0, 0, 0, 0,  /* factorial */
        OP_CALL, 1, 0,            /* factorial(n-1) */
        OP_MUL,                   /* n * result */
        OP_RETURN,
        /* base case (offset 49): */
        OP_PUSH_INT, 1, 0, 0, 0, 0, 0, 0, 0,  /* 1 */
        OP_RETURN,

        /* Main (offset 59): */
        OP_PUSH_INT, 5, 0, 0, 0, 0, 0, 0, 0,  /* factorial(5) */
        OP_PUSH_CONST, 0, 0, 0, 0,  /* function */
        OP_CALL, 1, 0,            /* CALL */
        OP_HALT
    };

    vm_load_code(vm, code, sizeof(code));

    /* Disassemble for debugging */
    printf("\nBytecode disassembly:\n");
    disassemble_code(code, sizeof(code), "test_recursive_factorial");
    printf("\n");

    vm->pc = 0x39;  /* Start at main (offset 57 - see disassembly) */
    vm_run(vm);

    ASSERT(!vm->error, "Should not error");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 120, "5! = 120");

    object_release(fn);
    vm_free(vm);
    memory_shutdown();
    return NULL;
}

/* Test suite */
static const char* all_tests() {
    RUN_TEST(test_function_create);
    RUN_TEST(test_closure_create);
    RUN_TEST(test_simple_function_call);
    RUN_TEST(test_function_with_args);
    RUN_TEST(test_function_with_locals);
    RUN_TEST(test_recursive_factorial);
    return NULL;
}

int main(void) {
    RUN_SUITE(all_tests);
    return 0;
}
