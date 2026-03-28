/* Task - Lightweight green thread for cooperative multitasking
 *
 * A Task owns a VM and runs a function to completion, yielding
 * periodically to allow other tasks to execute.
 */

#ifndef BEERLANG_TASK_H
#define BEERLANG_TASK_H

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
