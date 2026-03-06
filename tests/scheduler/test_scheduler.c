/* Tests for scheduler */

#include "test.h"
#include "beerlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * eval helpers (same pattern as test_core.c)
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
    vm->scheduler = global_scheduler;
    vm_run(vm);

    Value result = VALUE_NIL;
    if (vm->error) {
        *err = true;
    } else if (!vm_stack_empty(vm)) {
        result = vm->stack[vm->stack_pointer - 1];
        if (is_pointer(result)) object_retain(result);
        vm_pop(vm);
    }

    if (global_scheduler) {
        scheduler_run_until_done(global_scheduler);
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

/* ================================================================
 * Tests
 * ================================================================ */

TEST(scheduler_create) {
    eval_init();
    ASSERT(global_scheduler != NULL, "global_scheduler should exist after namespace_init");
    ASSERT_EQ(global_scheduler->ready_count, 0, "ready_count should be 0 initially");
    eval_cleanup();
    return NULL;
}

TEST(scheduler_spawn_and_run) {
    eval_init();
    bool err;
    Value v = eval_one("(await (spawn (fn [] 42)))", &err);
    ASSERT(!err, "spawn+await should not error");
    ASSERT_EQ(untag_fixnum(v), 42, "spawned task should return 42");
    eval_cleanup();
    return NULL;
}

TEST(scheduler_multiple_tasks) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def t1 (spawn (fn [] 10)))",
        "(def t2 (spawn (fn [] 20)))",
        "(def t3 (spawn (fn [] 30)))",
        "(+ (+ (await t1) (await t2)) (await t3))",
    };
    Value v = eval_multi(exprs, 4, &err);
    ASSERT(!err, "multiple tasks should not error");
    ASSERT_EQ(untag_fixnum(v), 60, "sum of tasks should be 60");
    eval_cleanup();
    return NULL;
}

TEST(scheduler_round_robin) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def t1 (spawn (fn [] (yield) (yield) 1)))",
        "(def t2 (spawn (fn [] (yield) (yield) 2)))",
        "(+ (await t1) (await t2))",
    };
    Value v = eval_multi(exprs, 3, &err);
    ASSERT(!err, "round-robin tasks should not error");
    ASSERT_EQ(untag_fixnum(v), 3, "sum of yielding tasks should be 3");
    eval_cleanup();
    return NULL;
}

/* ================================================================ */

static const char* all_tests(void) {
    RUN_TEST(scheduler_create);
    RUN_TEST(scheduler_spawn_and_run);
    RUN_TEST(scheduler_multiple_tasks);
    RUN_TEST(scheduler_round_robin);
    return NULL;
}

int main(void) {
    printf("Testing scheduler...\n\n");
    const char* result = all_tests();
    printf("\nall_tests\n");
    printf("==========================================\n");
    if (result) {
        printf("FAIL: %s\n", result);
        return 1;
    }
    printf("All scheduler tests passed!\n");
    return 0;
}
