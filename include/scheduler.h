/* Scheduler - Cooperative task scheduler
 *
 * Maintains a ready queue of tasks and runs them round-robin.
 * Each task gets a configurable instruction quota before yielding.
 */

#ifndef BEERLANG_SCHEDULER_H
#define BEERLANG_SCHEDULER_H

#include "value.h"
#include "task.h"

/* Forward declarations */
typedef struct IOReactor IOReactor;

/* Default instruction quota per task quantum */
#define DEFAULT_TASK_QUOTA 1000

/* Scheduler structure */
typedef struct Scheduler {
    /* Ready queue (doubly-linked circular list with sentinel) */
    Task ready_head;    /* Sentinel node (not a real task) */
    int ready_count;

    /* Currently running task */
    Task* current;

    /* I/O reactor (async fd monitoring) */
    IOReactor* io_reactor;
    int blocked_count;      /* Tasks blocked on I/O */

    /* Configuration */
    int quota;          /* Instructions per quantum */
} Scheduler;

/* Create a new scheduler */
Scheduler* scheduler_new(int quota);

/* Free scheduler (does NOT free tasks — they are refcounted) */
void scheduler_free(Scheduler* sched);

/* Spawn a new task: create task for fn(args...), enqueue to ready */
Value scheduler_spawn(Scheduler* sched, Value fn, int argc, Value* argv);

/* Run all tasks until ready queue is empty */
void scheduler_run_until_done(Scheduler* sched);

/* Run one task for one quantum (for REPL integration) */
bool scheduler_run_one_tick(Scheduler* sched);

/* Check if there are ready tasks */
bool scheduler_has_ready(Scheduler* sched);

/* Queue management (used by channel/await ops) */
void scheduler_enqueue(Scheduler* sched, Task* task);
void scheduler_block(Scheduler* sched, Task* task);
void scheduler_wake(Scheduler* sched, Task* task);
void scheduler_block_io(Scheduler* sched, Task* task);
void scheduler_wake_io(Scheduler* sched, Task* task);

/* Global scheduler instance */
extern Scheduler* global_scheduler;

#endif /* BEERLANG_SCHEDULER_H */
