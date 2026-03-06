/* Compiler Tests for fn (Function Definitions) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "beerlang.h"

/* Helper macros for type checking */
#define is_function(v) (is_pointer(v) && object_type(v) == TYPE_FUNCTION)

/* Helper: compile and check for errors */
static CompiledCode* compile_string(const char* source) {
    memory_init();
    symbol_init();
    namespace_init();

    /* Read source */
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
 * Basic fn Compilation Tests
 * ================================================================= */

/* Test: (fn [] 42) - simple function with no parameters */
TEST(test_compile_fn_no_args) {
    CompiledCode* code = compile_string("(fn [] 42)");
    ASSERT(code != NULL, "Should compile fn with no args");

    /* Should generate:
     * PUSH_CONST <idx>  ; push function object
     * HALT
     *
     * Function body should be:
     * ENTER 0
     * PUSH_INT 42
     * RETURN
     */

    /* Check that we have a constant (the function) */
    ASSERT(vector_length(code->constants) >= 1, "Should have at least one constant");

    Value fn_const = vector_get(code->constants, 0);
    ASSERT(is_function(fn_const), "First constant should be a function");
    ASSERT_EQ(function_arity(fn_const), 0, "Function arity should be 0");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (fn [x] x) - identity function with one parameter */
TEST(test_compile_fn_one_arg) {
    CompiledCode* code = compile_string("(fn [x] x)");
    ASSERT(code != NULL, "Should compile fn with one arg");

    /* Function body should be:
     * ENTER 0           ; no additional locals (just the param)
     * LOAD_LOCAL 0      ; load parameter x
     * RETURN
     */

    ASSERT(vector_length(code->constants) >= 1, "Should have at least one constant");

    Value fn_const = vector_get(code->constants, 0);
    ASSERT(is_function(fn_const), "First constant should be a function");
    ASSERT_EQ(function_arity(fn_const), 1, "Function arity should be 1");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (fn [x y] (+ x y)) - function with multiple parameters */
TEST(test_compile_fn_two_args) {
    CompiledCode* code = compile_string("(fn [x y] (+ x y))");
    ASSERT(code != NULL, "Should compile fn with two args");

    /* Find the function constant (might not be at index 0 due to symbols) */
    Value fn_const = VALUE_NIL;
    for (size_t i = 0; i < vector_length(code->constants); i++) {
        Value v = vector_get(code->constants, i);
        if (is_function(v)) {
            fn_const = v;
            break;
        }
    }

    ASSERT(is_function(fn_const), "Should have a function constant");
    ASSERT_EQ(function_arity(fn_const), 2, "Function arity should be 2");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (fn add [x y] (+ x y)) - named function for recursion */
TEST(test_compile_fn_named) {
    CompiledCode* code = compile_string("(fn add [x y] (+ x y))");
    ASSERT(code != NULL, "Should compile named fn");

    /* Named functions bind the name in the function's environment,
     * allowing recursive calls
     */

    /* Find the function constant */
    Value fn_const = VALUE_NIL;
    for (size_t i = 0; i < vector_length(code->constants); i++) {
        Value v = vector_get(code->constants, i);
        if (is_function(v)) {
            fn_const = v;
            break;
        }
    }

    ASSERT(is_function(fn_const), "Should have a function constant");
    ASSERT_EQ(function_arity(fn_const), 2, "Function arity should be 2");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (fn [x] (fn [y] (+ x y))) - nested function (closure) */
TEST(test_compile_fn_closure) {
    CompiledCode* code = compile_string("(fn [x] (fn [y] (+ x y)))");
    ASSERT(code != NULL, "Should compile nested fn");

    /* Outer function is a plain fn (no free vars) → in constant pool.
     * Inner function captures x → created at runtime via OP_MAKE_CLOSURE,
     * so only the outer fn appears as a function constant. */

    Value outer_fn = VALUE_NIL;
    int fn_count = 0;

    for (size_t i = 0; i < vector_length(code->constants); i++) {
        Value v = vector_get(code->constants, i);
        if (is_function(v)) {
            if (fn_count == 0) {
                outer_fn = v;
            }
            fn_count++;
        }
    }

    ASSERT_EQ(fn_count, 1, "Should have 1 function constant (outer fn only)");
    ASSERT(is_function(outer_fn), "Should have outer function");
    ASSERT_EQ(function_arity(outer_fn), 1, "Outer function arity should be 1");

    /* The inner closure is created at runtime via OP_MAKE_CLOSURE.
     * Verify that the bytecode contains an OP_MAKE_CLOSURE instruction (0x80). */
    bool found_make_closure = false;
    for (size_t i = 0; i < code->code_size; i++) {
        if (code->bytecode[i] == 0x80) {  /* OP_MAKE_CLOSURE */
            found_make_closure = true;
            break;
        }
    }
    ASSERT(found_make_closure, "Bytecode should contain OP_MAKE_CLOSURE for inner fn");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (fn [x] (+ x 1)) with multiple expressions in body */
TEST(test_compile_fn_implicit_do) {
    /* Functions with multiple body expressions should act like (do ...) */
    CompiledCode* code = compile_string("(fn [x] (println x) (+ x 1))");
    ASSERT(code != NULL, "Should compile fn with multiple body expressions");

    /* Find the function constant */
    Value fn_const = VALUE_NIL;
    for (size_t i = 0; i < vector_length(code->constants); i++) {
        Value v = vector_get(code->constants, i);
        if (is_function(v)) {
            fn_const = v;
            break;
        }
    }

    ASSERT(is_function(fn_const), "Should have a function constant");
    ASSERT_EQ(function_arity(fn_const), 1, "Function arity should be 1");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* =================================================================
 * Runtime Execution Tests
 * ================================================================= */

/* Test: Execute (fn [] 42) */
TEST(test_fn_runtime_no_args) {
    CompiledCode* code = compile_string("((fn [] 42))");
    ASSERT(code != NULL, "Should compile fn call");

    /* Create VM and run */
    VM* vm = vm_new(256);

    /* Load code and constants */
    vm_load_code(vm, code->bytecode, code->code_size);

    /* Convert vector to C array for VM */
    int n_constants = (int)vector_length(code->constants);
    Value* constants = malloc(n_constants * sizeof(Value));
    for (int i = 0; i < n_constants; i++) {
        constants[i] = vector_get(code->constants, i);
    }
    vm_load_constants(vm, constants, n_constants);

    /* Run */
    vm_run(vm);

    ASSERT(!vm->error, "VM should not error");
    ASSERT(!vm_stack_empty(vm), "Result should be on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "Result should be 42");

    free(constants);
    vm_free(vm);
    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: Execute ((fn [x] (+ x 1)) 41) */
/* NOTE: Disabled until native functions (+) are implemented */
TEST(test_fn_runtime_with_arg) {
    /* TODO: Re-enable when native functions are available */
    return NULL;  /* Skip this test for now */

    #if 0
    CompiledCode* code = compile_string("((fn [x] (+ x 1)) 41)");
    ASSERT(code != NULL, "Should compile fn call with arg");

    /* Create VM and run */
    VM* vm = vm_new(256);

    vm_load_code(vm, code->bytecode, code->code_size);

    int n_constants = (int)vector_length(code->constants);
    Value* constants = malloc(n_constants * sizeof(Value));
    for (int i = 0; i < n_constants; i++) {
        constants[i] = vector_get(code->constants, i);
    }
    vm_load_constants(vm, constants, n_constants);

    vm_run(vm);

    ASSERT(!vm->error, "VM should not error");
    ASSERT(!vm_stack_empty(vm), "Result should be on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result), "Result should be fixnum");
    ASSERT_EQ(untag_fixnum(result), 42, "Result should be 42");

    free(constants);
    vm_free(vm);
    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
    #endif  /* Disabled test */
}

/* =================================================================
 * Error Handling Tests
 * ================================================================= */

/* Test: (fn) - missing parameter vector */
TEST(test_fn_error_missing_params) {
    CompiledCode* code = compile_string("(fn)");
    ASSERT(code == NULL, "Should not compile fn without params");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (fn 42) - invalid parameter vector */
TEST(test_fn_error_invalid_params) {
    CompiledCode* code = compile_string("(fn 42)");
    ASSERT(code == NULL, "Should not compile fn with non-vector params");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (fn [x]) - missing body */
TEST(test_fn_error_missing_body) {
    CompiledCode* code = compile_string("(fn [x])");
    ASSERT(code == NULL, "Should not compile fn without body");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* =================================================================
 * Test Suite
 * ================================================================= */

static const char* all_tests(void) {
    RUN_TEST(test_compile_fn_no_args);
    RUN_TEST(test_compile_fn_one_arg);
    RUN_TEST(test_compile_fn_two_args);
    RUN_TEST(test_compile_fn_named);
    RUN_TEST(test_compile_fn_closure);
    RUN_TEST(test_compile_fn_implicit_do);
    RUN_TEST(test_fn_runtime_no_args);
    RUN_TEST(test_fn_runtime_with_arg);
    RUN_TEST(test_fn_error_missing_params);
    RUN_TEST(test_fn_error_invalid_params);
    RUN_TEST(test_fn_error_missing_body);
    return NULL;
}

int main(void) {
    printf("Testing fn compilation...\n");
    RUN_SUITE(all_tests);
    return 0;
}
