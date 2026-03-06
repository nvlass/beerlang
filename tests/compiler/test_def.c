/* Tests for def special form compilation */

#include "test.h"
#include "beerlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to compile a string */
static CompiledCode* compile_string(const char* source) {
    memory_init();
    symbol_init();
    namespace_init();  /* Initialize namespace system */

    /* Use Reader API directly */
    Reader* r = reader_new(source, "<test>");
    Value form = reader_read(r);

    if (reader_has_error(r)) {
        fprintf(stderr, "Failed to read: %s\n", source);
        reader_free(r);
        namespace_shutdown();
        memory_shutdown();
        return NULL;
    }
    reader_free(r);

    Compiler* c = compiler_new("<test>");
    CompiledCode* code = compile(c, form);

    compiler_free(c);
    if (!is_nil(form)) object_release(form);

    namespace_shutdown();

    return code;
}

TEST(test_compile_def_simple) {
    CompiledCode* code = compile_string("(def x 42)");

    ASSERT(code != NULL, "Should compile (def x 42)");

    /* Expected bytecode:
     * PUSH_INT 42
     * STORE_VAR 0 (symbol 'x' at const index 0)
     * HALT
     */

    ASSERT_EQ(code->bytecode[0], OP_PUSH_INT, "Should emit PUSH_INT");

    /* Find STORE_VAR (after 9-byte PUSH_INT instruction) */
    ASSERT_EQ(code->bytecode[9], OP_STORE_VAR, "Should emit STORE_VAR");

    /* Check that symbol 'x' is in constants */
    ASSERT(!is_nil(code->constants), "Should have constants");
    int n_consts = (int)vector_length(code->constants);
    ASSERT(n_consts >= 1, "Should have at least 1 constant");

    Value sym = vector_get(code->constants, 0);
    ASSERT(is_pointer(sym) && object_type(sym) == TYPE_SYMBOL, "First constant should be symbol");
    ASSERT(strcmp(symbol_name(sym), "x") == 0, "Symbol should be 'x'");

    compiled_code_free(code);
    return NULL;
}

TEST(test_compile_def_expression) {
    CompiledCode* code = compile_string("(def y (+ 1 2))");

    ASSERT(code != NULL, "Should compile (def y (+ 1 2))");

    /* Expected bytecode:
     * PUSH_INT 1
     * PUSH_INT 2
     * LOAD_VAR <symbol '+'>  ; Load + function from global
     * CALL 2
     * STORE_VAR <symbol 'y'>
     * HALT
     */

    /* Just verify it has the key opcodes */
    bool has_store_var = false;
    for (size_t i = 0; i < code->code_size; i++) {
        if (code->bytecode[i] == OP_STORE_VAR) {
            has_store_var = true;
            break;
        }
    }
    ASSERT(has_store_var, "Should have STORE_VAR instruction");

    compiled_code_free(code);
    return NULL;
}

TEST(test_def_runtime) {
    /* Test actual execution of def */
    memory_init();
    symbol_init();
    namespace_init();

    /* Compile (def x 42) */
    Reader* r = reader_new("(def x 42)", "<test>");
    Value form = reader_read(r);
    ASSERT(!reader_has_error(r), "Should read form");
    reader_free(r);

    Compiler* c = compiler_new("<test>");
    CompiledCode* code = compile(c, form);
    ASSERT(code != NULL, "Should compile");
    compiler_free(c);
    if (!is_nil(form)) object_release(form);

    /* Execute in VM */
    VM* vm = vm_new(256);
    vm_load_code(vm, code->bytecode, (int)code->code_size);

    /* Convert constants vector to array for VM */
    int n_consts = (int)vector_length(code->constants);
    Value* const_array = (Value*)malloc(sizeof(Value) * n_consts);
    for (int i = 0; i < n_consts; i++) {
        const_array[i] = vector_get(code->constants, i);
    }
    vm_load_constants(vm, const_array, n_consts);

    vm_run(vm);

    ASSERT(!vm->error, "Should execute without error");
    ASSERT(vm->stack_pointer == 1, "Should have result on stack");

    Value result = vm_pop(vm);
    ASSERT(is_fixnum(result) && untag_fixnum(result) == 42, "Result should be 42");

    /* Check that var was defined in namespace */
    Namespace* ns = namespace_registry_current(global_namespace_registry);
    ASSERT(ns != NULL, "Should have current namespace");

    Value x_sym = symbol_intern("x");
    Var* x_var = namespace_lookup(ns, x_sym);
    ASSERT(x_var != NULL, "Var 'x' should be defined");

    Value x_value = var_get_value(x_var);
    ASSERT(is_fixnum(x_value) && untag_fixnum(x_value) == 42, "Var 'x' should have value 42");

    /* Don't release x_sym — symbol_intern returns unowned reference */
    free(const_array);
    vm_free(vm);
    compiled_code_free(code);
    namespace_shutdown();
    memory_shutdown();
    return NULL;
}

TEST(test_def_and_lookup) {
    /* Test defining a var and then looking it up */
    memory_init();
    symbol_init();
    namespace_init();

    /* Compile and execute (def x 10) */
    Reader* r1 = reader_new("(def x 10)", "<test>");
    Value form1 = reader_read(r1);
    reader_free(r1);

    Compiler* c1 = compiler_new("<test>");
    CompiledCode* code1 = compile(c1, form1);
    compiler_free(c1);
    if (!is_nil(form1)) object_release(form1);

    VM* vm1 = vm_new(256);
    vm_load_code(vm1, code1->bytecode, (int)code1->code_size);

    int n_consts1 = (int)vector_length(code1->constants);
    Value* const_array1 = (Value*)malloc(sizeof(Value) * n_consts1);
    for (int i = 0; i < n_consts1; i++) {
        const_array1[i] = vector_get(code1->constants, i);
    }
    vm_load_constants(vm1, const_array1, n_consts1);

    vm_run(vm1);
    ASSERT(!vm1->error, "First def should succeed");

    free(const_array1);
    vm_free(vm1);
    compiled_code_free(code1);

    /* Now compile and execute just 'x' (should load var) */
    Reader* r2 = reader_new("x", "<test>");
    Value form2 = reader_read(r2);
    reader_free(r2);

    Compiler* c2 = compiler_new("<test>");
    CompiledCode* code2 = compile(c2, form2);
    compiler_free(c2);
    if (!is_nil(form2)) object_release(form2);

    ASSERT(code2 != NULL, "Should compile symbol lookup");

    VM* vm2 = vm_new(256);
    vm_load_code(vm2, code2->bytecode, (int)code2->code_size);

    int n_consts2 = (int)vector_length(code2->constants);
    Value* const_array2 = (Value*)malloc(sizeof(Value) * n_consts2);
    for (int i = 0; i < n_consts2; i++) {
        const_array2[i] = vector_get(code2->constants, i);
    }
    vm_load_constants(vm2, const_array2, n_consts2);

    vm_run(vm2);
    ASSERT(!vm2->error, "Symbol lookup should succeed");
    ASSERT(vm2->stack_pointer == 1, "Should have result on stack");

    Value result = vm_pop(vm2);
    ASSERT(is_fixnum(result) && untag_fixnum(result) == 10, "Should load value 10");

    free(const_array2);
    vm_free(vm2);
    compiled_code_free(code2);
    namespace_shutdown();
    memory_shutdown();
    return NULL;
}

static const char* all_tests(void) {
    RUN_TEST(test_compile_def_simple);
    RUN_TEST(test_compile_def_expression);
    RUN_TEST(test_def_runtime);
    RUN_TEST(test_def_and_lookup);
    return NULL;
}

int main(void) {
    printf("Testing def special form...\n\n");

    const char* result = all_tests();

    printf("\nall_tests\n");
    printf("==========================================\n");

    if (result) {
        printf("FAIL: %s\n", result);
        return 1;
    }

    printf("All def tests passed! ✓\n");
    return 0;
}
