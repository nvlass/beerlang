/* Task - Lightweight green thread for cooperative multitasking
 *
 * A Task owns a VM and runs a function to completion, yielding
 * periodically to allow other tasks to execute.
 */

#ifndef BEERLANG_TASK_H
#define BEERLANG_TASK_H

#include <signal.h>
#include "value.h"

/* Forward declarations */
typedef struct VM VM;
typedef struct Scheduler Scheduler;

/* Task states */
typedef enum {
    TASK_READY,     /* Runnable, waiting in ready queue */
    TASK_RUNNING,   /* Currently executing */
    TASK_BLOCKED,   /* Waiting on channel or await */
    TASK_DONE,      /* Completed execution */
} TaskState;

/* Watcher node — singly-linked list of callbacks to fire on task completion */
typedef struct WatcherNode {
    Value callback;           /* Function to call — retained */
    struct WatcherNode* next;
} WatcherNode;

/* Task object structure */
typedef struct Task {
    struct Object header;
    VM* vm;                 /* Owned VM instance */
    TaskState state;
    Value result;           /* Result value when DONE */

    /* Queue linkage (intrusive doubly-linked list) */
    struct Task* next;
    struct Task* prev;

    /* Channel blocking info */
    Value blocked_value;    /* Value being sent (for blocked senders) */
    struct Task* wait_queue; /* Sentinel of the channel queue it waits in */
    bool io_blocked;        /* Blocked via scheduler_block_io (I/O or sleep) */

    /* Interrupts (see vm_request_interrupt) */
    bool foreground;        /* A run_task_to_completion target outside any background task */
    bool caller_reports;    /* A run_task_to_completion target: its caller reports errors */

    /* Dynamic var bindings: hashmap var key -> value, or nil. Inherited
     * (shared, persistent) by tasks created while this one runs. */
    Value bindings;
    struct Namespace* ns_cache;  /* namespace for the bound *ns* symbol below */
    Value ns_cache_sym;
    bool interrupt_pending; /* Raise the interrupt when it next runs */
    volatile sig_atomic_t interrupt_requested; /* set by vm_request_interrupt_task */
    struct Task* waiting_on; /* target of a run_task_to_completion it is inside */

    /* Completion watchers */
    WatcherNode* watchers;

    /* The bytecode/constants arrays this task allocated. vm->code and
     * vm->constants are swapped on every call, so a task that ends inside a
     * callee frame can't rely on them pointing at its own arrays. */
    uint8_t* own_code;
    Value*   own_constants;
    int      n_own_constants;
    /* task_new retains the fn/args it stores and must release them;
     * task_new_from_code borrows a compiled unit's values and must not. */
    bool owns_constant_values;
} Task;

/* Create a new task that will call fn with the given args.
 * Returns a tagged Value (TYPE_TASK). The task starts in READY state. */
Value task_new(Value fn, int argc, Value* argv, Scheduler* sched);

/* Create a task from pre-compiled bytecode.
 * Clones code and constants (task owns the copies). */
Value task_new_from_code(uint8_t* code, int code_size,
                         Value* constants, int n_constants,
                         Scheduler* sched);

/* Run the task for one quantum. Updates state to DONE or READY. */
void task_run(Task* task);

/* Free task resources (called by destructor) */
void task_destroy(struct Object* obj);

/* Add a watcher callback to be invoked when task completes */
void task_add_watcher(Task* task, Value callback);

/* Initialize task type (register destructor) */
void task_init(void);

/* Type check */
static inline bool is_task(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_TASK;
}

/* Accessors */
static inline Task* task_get(Value v) {
    return (Task*)untag_pointer(v);
}

#endif /* BEERLANG_TASK_H */
