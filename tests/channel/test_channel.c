/* Tests for channels */

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

    Value task_val = task_new_from_code(code->bytecode, (int)code->code_size,
                                         const_arr, n_consts, global_scheduler);
    Task* task = task_get(task_val);
    scheduler_run_task_to_completion(global_scheduler, task);

    if (global_scheduler) {
        scheduler_run_until_done(global_scheduler);
    }

    Value result = VALUE_NIL;
    if (task->vm->error) {
        *err = true;
    } else if (!vm_stack_empty(task->vm)) {
        result = task->vm->stack[task->vm->stack_pointer - 1];
        if (is_pointer(result)) object_retain(result);
        vm_pop(task->vm);
    }

    object_release(task_val);

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

TEST(channel_create) {
    eval_init();
    bool err;
    Value v = eval_one("(chan 5)", &err);
    ASSERT(!err, "chan should not error");
    ASSERT(is_channel(v), "chan should return a channel");
    if (is_pointer(v) && !is_nil(v)) object_release(v);
    eval_cleanup();
    return NULL;
}

TEST(channel_buffered_eval) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def c (chan 1))",
        "(>! c 42)",
        "(<! c)",
    };
    Value v = eval_multi(exprs, 3, &err);
    ASSERT(!err, "buffered channel send/recv should not error");
    ASSERT_EQ(untag_fixnum(v), 42, "should receive 42 from channel");
    eval_cleanup();
    return NULL;
}

TEST(channel_spawn_eval) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def c (chan 0))",
        "(spawn (fn [] (>! c 99)))",
        "(<! c)",
    };
    Value v = eval_multi(exprs, 3, &err);
    ASSERT(!err, "cross-task channel should not error");
    ASSERT_EQ(untag_fixnum(v), 99, "should receive 99 from spawned task");
    eval_cleanup();
    return NULL;
}

/* ================================================================ */

static const char* all_tests(void) {
    RUN_TEST(channel_create);
    RUN_TEST(channel_buffered_eval);
    RUN_TEST(channel_spawn_eval);
    return NULL;
}

int main(void) {
    printf("Testing channels...\n\n");
    const char* result = all_tests();
    printf("\nall_tests\n");
    printf("==========================================\n");
    if (result) {
        printf("FAIL: %s\n", result);
        return 1;
    }
    printf("All channel tests passed!\n");
    return 0;
}
