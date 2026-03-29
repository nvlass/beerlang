/* Task implementation - green thread with owned VM */

#include <stdlib.h>
#include <string.h>
#include "task.h"
#include "scheduler.h"
#include "vm.h"
#include "memory.h"
#include "function.h"
#include "native.h"

/* Task destructor */
void task_destroy(struct Object* obj) {
    Task* task = (Task*)obj;
    if (task->vm) {
        /* Free the mini bytecode and constants we allocated in task_new */
        free(task->vm->code);
        free(task->vm->constants);
        task->vm->code = NULL;
        task->vm->constants = NULL;
        vm_free(task->vm);
        task->vm = NULL;
    }
    if (is_pointer(task->result)) {
        object_release(task->result);
        task->result = VALUE_NIL;
    }
    if (is_pointer(task->blocked_value)) {
        object_release(task->blocked_value);
        task->blocked_value = VALUE_NIL;
    }
    /* Free any remaining watchers (shouldn't normally have any — fired on completion) */
    WatcherNode* w = task->watchers;
    while (w) {
        WatcherNode* next = w->next;
        if (is_pointer(w->callback)) {
            object_release(w->callback);
        }
        free(w);
        w = next;
    }
    task->watchers = NULL;
}

/* Initialize task type */
void task_init(void) {
    object_register_destructor(TYPE_TASK, task_destroy);
}

/* Create a new task */
Value task_new(Value fn, int argc, Value* argv, Scheduler* sched) {
    /* Allocate task object */
    size_t size = sizeof(Task);
    Task* task = (Task*)object_alloc(TYPE_TASK, size);
    Value task_val = tag_pointer(task);

    task->state = TASK_READY;
    task->result = VALUE_NIL;
    task->next = NULL;
    task->prev = NULL;
    task->blocked_value = VALUE_NIL;
    task->watchers = NULL;

    /* Create a VM for this task */
    VM* vm = vm_new(256);
    vm->scheduler = sched;
    vm->yield_countdown = sched ? sched->quota : 0;

    /* Build mini bytecode: PUSH_CONST arg0, ..., PUSH_CONST fn, CALL argc, HALT */
    int n_consts = argc + 1;
    size_t code_size = (size_t)(n_consts * 5 + 3 + 1);  /* 5 per PUSH_CONST, 3 for CALL, 1 for HALT */
    uint8_t* code = malloc(code_size);
    Value* consts = malloc(sizeof(Value) * (size_t)n_consts);

    /* Constants: args first, then fn */
    for (int i = 0; i < argc; i++) {
        consts[i] = argv[i];
    }
    consts[argc] = fn;

    /* Emit bytecode */
    size_t pc = 0;
    for (int i = 0; i < n_consts; i++) {
        code[pc++] = OP_PUSH_CONST;
        code[pc++] = (uint8_t)(i & 0xFF);
        code[pc++] = (uint8_t)((i >> 8) & 0xFF);
        code[pc++] = (uint8_t)((i >> 16) & 0xFF);
        code[pc++] = (uint8_t)((i >> 24) & 0xFF);
    }
    code[pc++] = OP_CALL;
    code[pc++] = (uint8_t)(argc & 0xFF);
    code[pc++] = (uint8_t)((argc >> 8) & 0xFF);
    code[pc++] = OP_HALT;

    vm_load_code(vm, code, (int)pc);
    vm_load_constants(vm, consts, n_consts);

    /* The task owns the code and constants memory.
     * We store them in the VM; they'll be freed when the task is destroyed
     * (vm_free doesn't free code/constants, so we need to handle this). */
    task->vm = vm;

    return task_val;
}

/* Create a task from pre-compiled bytecode */
Value task_new_from_code(uint8_t* code, int code_size,
                         Value* constants, int n_constants,
                         Scheduler* sched) {
    Task* task = (Task*)object_alloc(TYPE_TASK, sizeof(Task));
    Value task_val = tag_pointer(task);

    task->state = TASK_READY;
    task->result = VALUE_NIL;
    task->next = NULL;
    task->prev = NULL;
    task->blocked_value = VALUE_NIL;
    task->watchers = NULL;

    VM* vm = vm_new(256);
    vm->scheduler = sched;
    vm->yield_countdown = sched ? sched->quota : 0;

    /* Clone code and constants so the task owns them */
    uint8_t* code_copy = malloc((size_t)code_size);
    memcpy(code_copy, code, (size_t)code_size);
    Value* consts_copy = n_constants > 0
        ? malloc(sizeof(Value) * (size_t)n_constants) : NULL;
    for (int i = 0; i < n_constants; i++) {
        consts_copy[i] = constants[i];
    }

    vm_load_code(vm, code_copy, code_size);
    vm_load_constants(vm, consts_copy, n_constants);
    task->vm = vm;

    return task_val;
}

/* Run the task for one quantum */
void task_run(Task* task) {
    if (task->state != TASK_READY) return;

    task->state = TASK_RUNNING;

    VM* vm = task->vm;
    vm->yielded = false;

    /* Reset countdown for this quantum */
    if (vm->scheduler) {
        vm->yield_countdown = vm->scheduler->quota;
    }

    vm_run(vm);

    if (vm->error) {
        /* Task errored — treat as done with nil result */
        task->state = TASK_DONE;
        task->result = VALUE_NIL;
    } else if (!vm->running && !vm->yielded) {
        /* VM halted — task is done */
        task->state = TASK_DONE;
        if (vm->stack_pointer > 0) {
            task->result = vm->stack[vm->stack_pointer - 1];
            if (is_pointer(task->result)) {
                object_retain(task->result);
            }
        }
    } else if (vm->yielded) {
        /* Task yielded — back to ready (unless already blocked by channel op) */
        if (task->state != TASK_BLOCKED) {
            task->state = TASK_READY;
        }
    }
}

/* Add a watcher callback to fire when task completes */
void task_add_watcher(Task* task, Value callback) {
    WatcherNode* node = malloc(sizeof(WatcherNode));
    node->callback = callback;
    if (is_pointer(callback)) {
        object_retain(callback);
    }
    node->next = task->watchers;
    task->watchers = node;
}
