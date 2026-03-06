/* Compiler Tests for let (Local Bindings) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "beerlang.h"
#include "function.h"
#include "cons.h"

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
 * Basic let Compilation Tests
 * ================================================================= */

/* Test: (let* [] 42) - empty bindings */
TEST(test_compile_let_empty) {
    CompiledCode* code = compile_string("(let* [] 42)");
    ASSERT(code != NULL, "Should compile let with empty bindings");

    /* Top-level let is wrapped in a synthetic function */
    /* Don't check specific bytecode layout, just that it compiles */
    ASSERT(code->code_size > 0, "Should generate bytecode");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* [x 10] x) - single binding */
TEST(test_compile_let_single_binding) {
    CompiledCode* code = compile_string("(let* [x 10] x)");
    ASSERT(code != NULL, "Should compile let with single binding");

    /* Should have local variable */
    ASSERT_EQ(code->n_locals, 1, "Should have 1 local variable");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* [x 10 y 20] (+ x y)) - multiple bindings */
TEST(test_compile_let_multiple_bindings) {
    CompiledCode* code = compile_string("(let* [x 10 y 20] (+ x y))");
    ASSERT(code != NULL, "Should compile let with multiple bindings");

    /* Should have 2 local variables */
    ASSERT_EQ(code->n_locals, 2, "Should have 2 local variables");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* [x 10 y x] y) - later binding references earlier */
TEST(test_compile_let_sequential) {
    CompiledCode* code = compile_string("(let* [x 10 y x] y)");
    ASSERT(code != NULL, "Should compile let with sequential bindings");

    /* y's value expression should reference x */
    ASSERT_EQ(code->n_locals, 2, "Should have 2 local variables");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* [x 10] (println x) (+ x 1)) - implicit do in body */
TEST(test_compile_let_implicit_do) {
    CompiledCode* code = compile_string("(let* [x 10] (println x) (+ x 1))");
    ASSERT(code != NULL, "Should compile let with multiple body expressions");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: nested let */
TEST(test_compile_let_nested) {
    CompiledCode* code = compile_string("(let* [x 10] (let* [y 20] (+ x y)))");
    ASSERT(code != NULL, "Should compile nested let");

    /* Outer x and inner y should be separate locals */
    ASSERT_EQ(code->n_locals, 2, "Should have 2 total local variables");

    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* =================================================================
 * Runtime Execution Tests
 * ================================================================= */

/* Test: Execute (let* [x 42] x) */
TEST(test_let_runtime_single) {
    CompiledCode* code = compile_string("(let* [x 42] x)");
    ASSERT(code != NULL, "Should compile let");

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
}

/* Test: Execute (let* [x 10 y 20] (+ x y)) */
/* NOTE: Requires native + function, skip for now */
TEST(test_let_runtime_multiple) {
    /* TODO: Re-enable when native functions are available */
    return NULL;  /* Skip this test for now */

    #if 0
    CompiledCode* code = compile_string("(let* [x 10 y 20] (+ x y))");
    ASSERT(code != NULL, "Should compile let");

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
    ASSERT_EQ(untag_fixnum(result), 30, "Result should be 30");

    free(constants);
    vm_free(vm);
    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
    #endif
}

/* Test: Execute (let* [x 10 y x] y) - sequential bindings */
TEST(test_let_runtime_sequential) {
    CompiledCode* code = compile_string("(let* [x 10 y x] y)");
    ASSERT(code != NULL, "Should compile let");

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
    ASSERT_EQ(untag_fixnum(result), 10, "Result should be 10");

    free(constants);
    vm_free(vm);
    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* =================================================================
 * Heap Object Return Tests (regression: use-after-free in OP_RETURN)
 * ================================================================= */

/* Helper: compile, execute, and return result (retains before pop) */
static Value compile_and_run(const char* source, CompiledCode** out_code,
                             Value** out_constants, VM** out_vm) {
    CompiledCode* code = compile_string(source);
    if (!code) return VALUE_NIL;

    int n_constants = (int)vector_length(code->constants);
    Value* constants = malloc(n_constants * sizeof(Value));
    for (int i = 0; i < n_constants; i++) {
        constants[i] = vector_get(code->constants, i);
    }

    /* Set execution context on function objects (same as REPL) */
    for (int i = 0; i < n_constants; i++) {
        if (is_function(constants[i])) {
            function_set_code(constants[i],
                              code->bytecode, (int)code->code_size,
                              constants, n_constants);
        }
    }

    VM* vm = vm_new(256);
    vm_load_code(vm, code->bytecode, (int)code->code_size);
    vm_load_constants(vm, constants, n_constants);
    vm_run(vm);

    Value result = VALUE_NIL;
    if (!vm->error && !vm_stack_empty(vm)) {
        /* Retain before pop to prevent use-after-free */
        result = vm->stack[vm->stack_pointer - 1];
        if (is_pointer(result)) object_retain(result);
        vm_pop(vm);
    }

    *out_code = code;
    *out_constants = constants;
    *out_vm = vm;
    return result;
}

static void cleanup_run(CompiledCode* code, Value* constants, VM* vm, Value result) {
    if (is_pointer(result)) object_release(result);
    free(constants);
    vm_free(vm);
    compiled_code_free(code);
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
}

/* Test: (let* [x (list 1 2 3)] x) - return list from let */
TEST(test_let_return_heap_object) {
    CompiledCode* code; Value* constants; VM* vm;
    Value result = compile_and_run("(let* [x (list 1 2 3)] x)",
                                   &code, &constants, &vm);

    ASSERT(!vm->error, "VM should not error");
    ASSERT(is_cons(result), "Result should be a cons (list)");
    ASSERT(is_fixnum(car(result)), "First element should be fixnum");
    ASSERT_EQ(untag_fixnum(car(result)), 1, "First element should be 1");

    cleanup_run(code, constants, vm, result);
    return NULL;
}

/* Test: (let* [x (conj (list) 1)] x) - conj result from let */
TEST(test_let_return_conj_result) {
    CompiledCode* code; Value* constants; VM* vm;
    Value result = compile_and_run("(let* [x (conj (list) 1)] x)",
                                   &code, &constants, &vm);

    ASSERT(!vm->error, "VM should not error");
    ASSERT(is_cons(result), "Result should be a cons (list)");
    ASSERT_EQ(untag_fixnum(car(result)), 1, "Element should be 1");
    ASSERT(is_nil(cdr(result)), "Tail should be nil");

    cleanup_run(code, constants, vm, result);
    return NULL;
}

/* Test: loop accumulating to a list */
TEST(test_loop_return_accumulated_list) {
    CompiledCode* code; Value* constants; VM* vm;
    Value result = compile_and_run(
        "(loop [i 3 acc (list)] (if (= i 0) acc (recur (- i 1) (conj acc i))))",
        &code, &constants, &vm);

    ASSERT(!vm->error, "VM should not error");
    ASSERT(is_cons(result), "Result should be a cons (list)");

    /* Should be (1 2 3) - conj prepends, so 1 was added last */
    ASSERT_EQ(untag_fixnum(car(result)), 1, "First element should be 1");
    Value rest1 = cdr(result);
    ASSERT(is_cons(rest1), "Should have second element");
    ASSERT_EQ(untag_fixnum(car(rest1)), 2, "Second element should be 2");
    Value rest2 = cdr(rest1);
    ASSERT(is_cons(rest2), "Should have third element");
    ASSERT_EQ(untag_fixnum(car(rest2)), 3, "Third element should be 3");
    ASSERT(is_nil(cdr(rest2)), "Should have 3 elements");

    cleanup_run(code, constants, vm, result);
    return NULL;
}

/* Test: (let* [s (str "hello" " world")] s) - return string from let */
TEST(test_let_return_string) {
    CompiledCode* code; Value* constants; VM* vm;
    Value result = compile_and_run("(let* [s (str \"hello\" \" world\")] s)",
                                   &code, &constants, &vm);

    ASSERT(!vm->error, "VM should not error");
    ASSERT(is_pointer(result) && object_type(result) == TYPE_STRING,
           "Result should be a string");
    ASSERT(strcmp(string_cstr(result), "hello world") == 0,
           "String should be 'hello world'");

    cleanup_run(code, constants, vm, result);
    return NULL;
}

/* =================================================================
 * Error Handling Tests
 * ================================================================= */

/* Test: (let) - missing bindings */
TEST(test_let_error_missing_bindings) {
    CompiledCode* code = compile_string("(let*)");
    ASSERT(code == NULL, "Should not compile let* without bindings");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* 42) - invalid bindings (not a vector) */
TEST(test_let_error_invalid_bindings) {
    CompiledCode* code = compile_string("(let* 42)");
    ASSERT(code == NULL, "Should not compile let with non-vector bindings");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* [x 10]) - missing body */
TEST(test_let_error_missing_body) {
    CompiledCode* code = compile_string("(let* [x 10])");
    ASSERT(code == NULL, "Should not compile let without body");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* [x]) - odd number of binding forms */
TEST(test_let_error_odd_bindings) {
    CompiledCode* code = compile_string("(let* [x])");
    ASSERT(code == NULL, "Should not compile let with odd number of binding forms");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* Test: (let* [42 10] 42) - non-symbol binding name */
TEST(test_let_error_non_symbol_binding) {
    CompiledCode* code = compile_string("(let* [42 10] 42)");
    ASSERT(code == NULL, "Should not compile let with non-symbol binding name");

    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
    return NULL;
}

/* =================================================================
 * Test Suite
 * ================================================================= */

static const char* all_tests(void) {
    RUN_TEST(test_compile_let_empty);
    RUN_TEST(test_compile_let_single_binding);
    RUN_TEST(test_compile_let_multiple_bindings);
    RUN_TEST(test_compile_let_sequential);
    RUN_TEST(test_compile_let_implicit_do);
    RUN_TEST(test_compile_let_nested);
    RUN_TEST(test_let_runtime_single);
    RUN_TEST(test_let_runtime_multiple);
    RUN_TEST(test_let_runtime_sequential);
    RUN_TEST(test_let_return_heap_object);
    RUN_TEST(test_let_return_conj_result);
    RUN_TEST(test_loop_return_accumulated_list);
    RUN_TEST(test_let_return_string);
    RUN_TEST(test_let_error_missing_bindings);
    RUN_TEST(test_let_error_invalid_bindings);
    RUN_TEST(test_let_error_missing_body);
    RUN_TEST(test_let_error_odd_bindings);
    RUN_TEST(test_let_error_non_symbol_binding);
    return NULL;
}

int main(void) {
    printf("Testing let compilation...\n");
    RUN_SUITE(all_tests);
    return 0;
}
