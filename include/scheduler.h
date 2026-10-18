/* Scheduler - Cooperative task scheduler
 *
 * Maintains a ready queue of tasks and runs them round-robin.
 * Each task gets a configurable quota of safe points (jumps and fn entries)
 * before yielding.
 */

#ifndef BEERLANG_SCHEDULER_H
#define BEERLANG_SCHEDULER_H

#include <stdint.h>
#include "value.h"
#include "task.h"

/* Forward declarations */
typedef struct IOReactor IOReactor;

/* Default quota per task quantum, counted at safe points (jumps and fn entries) */
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

    /* Innermost foreground run_task_to_completion target: the task an
     * interrupt stops */
    Task* foreground;

    /* I/O reactor (async fd monitoring) */
    IOReactor* io_reactor;
    int blocked_count;      /* Tasks blocked on I/O */

    /* Timer list (sleep/timeout) */
    SleepEntry* sleep_list;

    /* Configuration */
    int quota;          /* Safe points per quantum */

    unsigned ticks;     /* Quanta run, for spacing out I/O checks */
} Scheduler;

/* Check fds for readiness every this many ticks while tasks are ready
 * (power of two) */
#define IO_CHECK_EVERY 16

/* Create a new scheduler */
Scheduler* scheduler_new(int quota);

/* Free scheduler (does NOT free tasks — they are refcounted) */
void scheduler_free(Scheduler* sched);

/* Spawn a new task: create task for fn(args...), enqueue to ready */
Value scheduler_spawn(Scheduler* sched, Value fn, int argc, Value* argv);

/* Run all tasks until ready queue AND blocked queue are empty (blocks forever
 * if persistent background tasks like nREPL accept-loops are running).
 * Waits in kevent/epoll_wait when nothing is ready — no busy polling. */
void scheduler_run_until_done(Scheduler* sched);

/* Non-blocking variant: wake tasks whose I/O or timers are ready, drain all
 * currently-ready tasks, then return.  Safe to call from a game/render
 * loop — never hangs waiting for blocked I/O tasks. */
void scheduler_run_ready(Scheduler* sched);

/* Run tasks until fd (e.g. stdin) is readable. Ready tasks keep running
 * while waiting, and fd is checked between batches of ticks. Also returns
 * when an interrupt is requested with no foreground task to stop. */
void scheduler_run_until_readable(Scheduler* sched, int fd);

/* Interrupt a given task (same semantics as vm_request_interrupt: skips
 * catch, runs finally). vm_request_interrupt_task only sets flags, so it
 * is safe from a signal or IRQ handler; the caller keeps the task alive
 * until it is delivered (next quantum; at most 16 pending at once).
 * scheduler_interrupt_task delivers at once, from scheduler context;
 * false if the task is already done. */
void vm_request_interrupt_task(Task* task);
bool scheduler_interrupt_task(Scheduler* sched, Task* task);

/* Run a native's temp VM (apply, swap!, ...) to completion. When the
 * calling task is foreground, temp counts safe points like a task and an
 * interrupt raised meanwhile unwinds it (running its finally blocks) and
 * is rethrown in caller; returns false then, and the native should just
 * return. Otherwise the same as vm_run(temp). */
bool scheduler_run_nested(VM* caller, VM* temp);

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

/* For natives: true when running in a task that can be parked. */
bool scheduler_can_park(struct VM* vm);

/* For natives: block the current task until fd is readable (or writable if
 * `write`); the VM retries the native call when the task wakes. The native
 * must return right after calling this. */
void scheduler_park_native(struct VM* vm, int fd, bool write);

/* Wake task (scheduler_wake_io) at wake_at_ns without blocking it; used to
 * put a deadline on an fd wait. scheduler_cancel_timer drops its timers. */
void scheduler_add_timer(Scheduler* sched, Task* task, int64_t wake_at_ns);
void scheduler_cancel_timer(Scheduler* sched, Task* task);

/* Global scheduler instance */
extern Scheduler* global_scheduler;

#endif /* BEERLANG_SCHEDULER_H */
