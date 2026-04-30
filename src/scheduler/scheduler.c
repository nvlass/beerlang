/* Scheduler implementation - cooperative round-robin task scheduler */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "scheduler.h"
#include "io_reactor.h"
#include "task.h"
#include "vm.h"
#include "memory.h"
#include "hashmap.h"
#include "bstring.h"
#include "symbol.h"

/* Global scheduler instance */
Scheduler* global_scheduler = NULL;

/* Create a new scheduler */
Scheduler* scheduler_new(int quota) {
    Scheduler* sched = malloc(sizeof(Scheduler));
    if (!sched) return NULL;

    /* Initialize ready queue sentinel (circular doubly-linked) */
    sched->ready_head.next = NULL;
    sched->ready_head.prev = NULL;
    sched->ready_count = 0;
    sched->current = NULL;
    sched->io_reactor = io_reactor_new();
    sched->blocked_count = 0;
    sched->sleep_list = NULL;
    sched->quota = quota > 0 ? quota : DEFAULT_TASK_QUOTA;

    return sched;
}

/* Free scheduler */
void scheduler_free(Scheduler* sched) {
    if (!sched) return;
    if (sched->io_reactor) io_reactor_free(sched->io_reactor);
    /* Free any remaining sleep entries */
    SleepEntry* se = sched->sleep_list;
    while (se) {
        SleepEntry* next = se->next;
        free(se);
        se = next;
    }
    free(sched);
}

/* Enqueue a task to the ready queue (append to tail) */
void scheduler_enqueue(Scheduler* sched, Task* task) {
    task->state = TASK_READY;

    /* Retain: scheduler holds a reference while task is in queue */
    Value task_val = tag_pointer(task);
    object_retain(task_val);

    /* Append to end of singly-linked list */
    task->next = NULL;
    if (!sched->ready_head.next) {
        sched->ready_head.next = task;
        task->prev = NULL;
    } else {
        /* Find tail */
        Task* tail = sched->ready_head.next;
        while (tail->next) tail = tail->next;
        tail->next = task;
        task->prev = tail;
    }
    sched->ready_count++;
}

/* Dequeue a task from the ready queue (remove from head).
 * The caller inherits the scheduler's retain on the task. */
static Task* scheduler_dequeue(Scheduler* sched) {
    Task* task = sched->ready_head.next;
    if (!task) return NULL;

    sched->ready_head.next = task->next;
    if (task->next) {
        task->next->prev = NULL;
    }
    task->next = NULL;
    task->prev = NULL;
    sched->ready_count--;
    /* Don't release here — caller inherits the reference */
    return task;
}

/* Block a task (remove from ready, mark blocked) */
void scheduler_block(Scheduler* sched, Task* task) {
    (void)sched;
    task->state = TASK_BLOCKED;
    /* Task is already not in the ready queue when running */
}

/* Wake a blocked task (move to ready queue) */
void scheduler_wake(Scheduler* sched, Task* task) {
    if (task->state != TASK_BLOCKED) return;
    scheduler_enqueue(sched, task);
}

/* Block a task on I/O (tracked separately for scheduler loop) */
void scheduler_block_io(Scheduler* sched, Task* task) {
    task->state = TASK_BLOCKED;
    sched->blocked_count++;
    /* Retain to keep task alive while IO-blocked — paired release in scheduler_wake_io.
     * Without this, scheduler_run_one_tick releases the dequeue ref after blocking,
     * freeing spawned tasks that have no other owner. */
    Value task_val = tag_pointer(task);
    object_retain(task_val);
}

/* Wake an I/O-blocked task */
void scheduler_wake_io(Scheduler* sched, Task* task) {
    if (task->state != TASK_BLOCKED) {
        /* Stale reactor event: native_close already woke this task, and then kqueue
         * fired an EV_EOF/EV_ERROR for the just-closed fd. The successful wake already
         * decremented blocked_count and released the retain from scheduler_block_io.
         * Do NOT release again — that would be a double-release / use-after-free. */
        return;
    }
    sched->blocked_count--;
    scheduler_enqueue(sched, task);  /* enqueue retains */
    /* Release the retain from scheduler_block_io */
    Value task_val = tag_pointer(task);
    object_release(task_val);
}

/* Block a task until wake_at_ns (CLOCK_MONOTONIC nanoseconds) */
void scheduler_sleep(Scheduler* sched, Task* task, int64_t wake_at_ns) {
    SleepEntry* entry = malloc(sizeof(SleepEntry));
    if (!entry) return;
    entry->task      = task;
    entry->wake_at_ns = wake_at_ns;
    entry->next      = sched->sleep_list;
    sched->sleep_list = entry;
    /* Block with IO tracking so blocked_count is correct and task stays alive */
    scheduler_block_io(sched, task);
}

/* Wake any sleep entries whose deadline has passed */
static void scheduler_check_timers(Scheduler* sched) {
    if (!sched->sleep_list) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t now_ns = (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;

    SleepEntry** pp = &sched->sleep_list;
    while (*pp) {
        SleepEntry* e = *pp;
        if (now_ns >= e->wake_at_ns) {
            *pp = e->next;
            scheduler_wake_io(sched, e->task);
            free(e);
        } else {
            pp = &e->next;
        }
    }
}

/* Spawn a new task */
Value scheduler_spawn(Scheduler* sched, Value fn, int argc, Value* argv) {
    Value task_val = task_new(fn, argc, argv, sched);
    Task* task = task_get(task_val);
    scheduler_enqueue(sched, task);
    return task_val;
}

/* Check if there are ready tasks */
bool scheduler_has_ready(Scheduler* sched) {
    return sched->ready_count > 0;
}

/* Drain I/O reactor completions, waking blocked tasks */
static void scheduler_drain_io(Scheduler* sched) {
    if (!sched->io_reactor) return;
    Task* woken[32];
    int n = io_reactor_drain(sched->io_reactor, woken, 32);
    for (int i = 0; i < n; i++) {
        scheduler_wake_io(sched, woken[i]);
    }
}

/* Fire watcher callbacks for a completed task */
void scheduler_fire_watchers(Scheduler* sched, Task* task) {
    WatcherNode* w = task->watchers;
    if (!w) return;

    /* Build result map */
    Value result_map = hashmap_create_default();
    if (task->vm && task->vm->error) {
        /* Error case: {:status :error, :message "..."} */
        hashmap_set(result_map, keyword_intern("status"), keyword_intern("error"));
        const char* msg = task->vm->error_msg ? task->vm->error_msg : "unknown error";
        Value msg_val = string_from_cstr(msg);
        hashmap_set(result_map, keyword_intern("message"), msg_val);
        object_release(msg_val);
    } else {
        /* Success case: {:status :ok, :result <value>} */
        hashmap_set(result_map, keyword_intern("status"), keyword_intern("ok"));
        hashmap_set(result_map, keyword_intern("result"), task->result);
    }

    /* Spawn a callback task for each watcher.
     * task_new copies argv values into constants without retaining,
     * effectively taking ownership of one reference per spawn.
     * We retain once per spawn so each task owns a ref, then release
     * our original ref at the end. */
    while (w) {
        WatcherNode* next = w->next;
        object_retain(result_map);
        object_retain(w->callback);  /* task_new takes ownership */
        scheduler_spawn(sched, w->callback, 1, &result_map);
        /* Release the watcher node's ref (separate from the one task_new took) */
        if (is_pointer(w->callback)) {
            object_release(w->callback);
        }
        free(w);
        w = next;
    }
    task->watchers = NULL;

    object_release(result_map);  /* Release our original ref */
}

/* Run one task for one quantum */
bool scheduler_run_one_tick(Scheduler* sched) {
    scheduler_check_timers(sched);
    scheduler_drain_io(sched);

    Task* task = scheduler_dequeue(sched);
    if (!task) return false;

    sched->current = task;
    task_run(task);
    sched->current = NULL;

    if (task->state == TASK_DONE && task->watchers) {
        scheduler_fire_watchers(sched, task);
    }

    if (task->state == TASK_READY) {
        /* Yielded — re-enqueue (enqueue retains, so release our dequeue ref) */
        scheduler_enqueue(sched, task);
        Value task_val = tag_pointer(task);
        object_release(task_val);
    } else {
        /* DONE or BLOCKED — release the scheduler's reference from dequeue */
        Value task_val = tag_pointer(task);
        object_release(task_val);
    }

    return true;
}

/* Run a specific task to completion, also running other ready tasks */
void scheduler_run_task_to_completion(Scheduler* sched, Task* target) {
    Task* saved_current = sched->current;
    scheduler_enqueue(sched, target);
    int iters = 0;
    while (target->state != TASK_DONE && iters < 1000000) {
        bool ran = scheduler_run_one_tick(sched);
        if (!ran) {
            if (sched->blocked_count > 0) {
                usleep(1000);
            } else {
                break;
            }
        }
        iters++;
    }
    sched->current = saved_current;
}

/* Run all tasks until ready queue and blocked queue are empty */
void scheduler_run_until_done(Scheduler* sched) {
    int iterations = 0;
    while (scheduler_has_ready(sched) || sched->blocked_count > 0) {
        if (!scheduler_has_ready(sched) && sched->blocked_count > 0) {
            /* Only blocked tasks remain — drain I/O and brief sleep */
            scheduler_drain_io(sched);
            if (!scheduler_has_ready(sched)) {
                usleep(1000);  /* 1ms to avoid busy-spin */
            }
            iterations++;
            if (iterations > 1000000) {
                fprintf(stderr, "scheduler: iteration limit reached, blocked=%d\n",
                        sched->blocked_count);
                break;
            }
            continue;
        }
        scheduler_run_one_tick(sched);
        iterations++;
        if (iterations > 1000000) {
            fprintf(stderr, "scheduler: iteration limit reached, ready_count=%d\n", sched->ready_count);
            break;
        }
    }
}
