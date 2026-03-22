/* Tests for beer.shell/exec native */

#include "test.h"
#include "beerlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * eval helper (same pattern as test_core.c)
 * ================================================================ */

#define MAX_UNITS 64
static CompiledCode* kept_code[MAX_UNITS];
static Value*        kept_consts[MAX_UNITS];
static int           n_kept = 0;

static void eval_init(void) {
    memory_init();
    symbol_init();
    namespace_init();
    n_kept = 0;
}

static void eval_cleanup(void) {
    for (int i = 0; i < n_kept; i++) {
        compiled_code_free(kept_code[i]);
        free(kept_consts[i]);
    }
    n_kept = 0;
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
}

static Value eval_one(const char* source, bool* err) {
    *err = false;

    Reader* r = reader_new(source, "<test>");
    Value form = reader_read(r);
    if (reader_has_error(r)) {
        fprintf(stderr, "Read error: %s\n", reader_error_msg(r));
        reader_free(r);
        *err = true;
        return VALUE_NIL;
    }
    reader_free(r);

    Compiler* c = compiler_new("<test>");
    CompiledCode* code = compile(c, form);
    if (compiler_has_error(c) || !code) {
        fprintf(stderr, "Compile error: %s\n", compiler_error_msg(c));
        if (code) compiled_code_free(code);
        compiler_free(c);
        if (!is_nil(form)) object_release(form);
        *err = true;
        return VALUE_NIL;
    }
    compiler_free(c);
    if (!is_nil(form)) object_release(form);

    int n_consts = (int)vector_length(code->constants);
    Value* const_arr = malloc(sizeof(Value) * (n_consts > 0 ? n_consts : 1));
    for (int i = 0; i < n_consts; i++) {
        const_arr[i] = vector_get(code->constants, i);
    }
    for (int i = 0; i < n_consts; i++) {
        if (is_function(const_arr[i])) {
            function_set_code(const_arr[i],
                              code->bytecode, (int)code->code_size,
                              const_arr, n_consts);
        }
    }

    VM* vm = vm_new(256);
    vm_load_code(vm, code->bytecode, (int)code->code_size);
    vm_load_constants(vm, const_arr, n_consts);
    vm_run(vm);

    Value result = VALUE_NIL;
    if (vm->error) {
        fprintf(stderr, "Runtime error: %s\n", vm->error_msg);
        *err = true;
    } else if (!vm_stack_empty(vm)) {
        result = vm->stack[vm->stack_pointer - 1];
        if (is_pointer(result)) object_retain(result);
        vm_pop(vm);
    }

    vm_free(vm);

    if (n_kept < MAX_UNITS) {
        kept_code[n_kept] = code;
        kept_consts[n_kept] = const_arr;
        n_kept++;
    } else {
        compiled_code_free(code);
        free(const_arr);
    }

    return result;
}

static Value eval_multi(const char* exprs[], int count, bool* err) {
    Value result = VALUE_NIL;
    for (int i = 0; i < count; i++) {
        result = eval_one(exprs[i], err);
        if (*err) return VALUE_NIL;
    }
    return result;
}

static void release_result(Value v) {
    if (is_pointer(v) && !is_nil(v))
        object_release(v);
}

/* ================================================================
 * Tests
 * ================================================================ */

TEST(shell_exec_echo) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(require 'beer.shell :as 'shell)",
        "(shell/exec \"echo\" \"hello\")"
    };
    Value result = eval_multi(exprs, 2, &err);
    ASSERT(!err, "shell/exec echo should not error");
    ASSERT(is_pointer(result) && object_type(result) == TYPE_HASHMAP,
           "shell/exec should return a map");

    Value key_exit = keyword_intern("exit");
    Value exit_val = hashmap_get(result, key_exit);
    ASSERT(is_fixnum(exit_val) && untag_fixnum(exit_val) == 0,
           "exit code should be 0");

    Value key_out = keyword_intern("out");
    Value out_val = hashmap_get(result, key_out);
    ASSERT(is_string(out_val), ":out should be a string");
    ASSERT_STR_EQ(string_cstr(out_val), "hello\n", ":out should be 'hello\\n'");

    Value key_err = keyword_intern("err");
    Value err_val = hashmap_get(result, key_err);
    ASSERT(is_string(err_val), ":err should be a string");
    ASSERT_STR_EQ(string_cstr(err_val), "", ":err should be empty");

    release_result(result);
    eval_cleanup();
    return NULL;
}

TEST(shell_exec_nonzero_exit) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(require 'beer.shell :as 'shell)",
        "(shell/exec \"false\")"
    };
    Value result = eval_multi(exprs, 2, &err);
    ASSERT(!err, "shell/exec false should not error");

    Value key_exit = keyword_intern("exit");
    Value exit_val = hashmap_get(result, key_exit);
    ASSERT(is_fixnum(exit_val) && untag_fixnum(exit_val) != 0,
           "exit code should be non-zero");

    release_result(result);
    eval_cleanup();
    return NULL;
}

TEST(shell_exec_stderr) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(require 'beer.shell :as 'shell)",
        "(shell/exec \"sh\" \"-c\" \"echo oops >&2\")"
    };
    Value result = eval_multi(exprs, 2, &err);
    ASSERT(!err, "shell/exec stderr should not error");

    Value key_err = keyword_intern("err");
    Value err_val = hashmap_get(result, key_err);
    ASSERT(is_string(err_val), ":err should be a string");
    ASSERT_STR_EQ(string_cstr(err_val), "oops\n", ":err should contain 'oops\\n'");

    release_result(result);
    eval_cleanup();
    return NULL;
}

static const char* all_tests(void) {
    RUN_TEST(shell_exec_echo);
    RUN_TEST(shell_exec_nonzero_exit);
    RUN_TEST(shell_exec_stderr);
    return NULL;
}

int main(void) {
    printf("Testing beer.shell natives...\n\n");

    const char* result = all_tests();

    printf("\nall_tests\n");
    printf("==========================================\n");

    if (result) {
        printf("FAIL: %s\n", result);
        return 1;
    }

    printf("All shell tests passed!\n");
    return 0;
}
