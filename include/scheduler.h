/* Scheduler - Cooperative task scheduler
 *
 * Maintains a ready queue of tasks and runs them round-robin.
 * Each task gets a configurable instruction quota before yielding.
 */

#ifndef BEERLANG_SCHEDULER_H
#define BEERLANG_SCHEDULER_H

#include <stdint.h>
#include "value.h"
#include "task.h"

/* Forward declarations */
typedef struct IOReactor IOReactor;

/* Default instruction quota per task quantum */
#define DEFAULT_TASK_QUOTA 1000

/* Timer entry — for sleep/after-delay */
typedef struct SleepEntry {
    Task*               task;
    int64_t             wake_at_ns;  /* CLOCK_MONOTONIC nanoseconds */
    struct SleepEntry*  next;
} SleepEntry;

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

    /* Timer list (sleep/timeout) */
    SleepEntry* sleep_list;

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

/* Run a specific task to completion (may run other tasks too).
 * Saves/restores sched->current for safe re-entrant use from natives. */
void scheduler_run_task_to_completion(Scheduler* sched, Task* target);

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

/* Fire watcher callbacks for a completed task */
void scheduler_fire_watchers(Scheduler* sched, Task* task);

/* Sleep: block task until wake_at_ns (CLOCK_MONOTONIC nanoseconds) */
void scheduler_sleep(Scheduler* sched, Task* task, int64_t wake_at_ns);

/* Global scheduler instance */
extern Scheduler* global_scheduler;

#endif /* BEERLANG_SCHEDULER_H */
