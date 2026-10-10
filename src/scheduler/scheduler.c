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
    sched->ticks = 0;

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

static int64_t now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

void scheduler_add_timer(Scheduler* sched, Task* task, int64_t wake_at_ns) {
    SleepEntry* entry = malloc(sizeof(SleepEntry));
    if (!entry) return;
    entry->task      = task;
    entry->wake_at_ns = wake_at_ns;
    entry->next      = sched->sleep_list;
    sched->sleep_list = entry;
}

void scheduler_cancel_timer(Scheduler* sched, Task* task) {
    SleepEntry** pp = &sched->sleep_list;
    while (*pp) {
        SleepEntry* e = *pp;
        if (e->task == task) {
            *pp = e->next;
            free(e);
        } else {
            pp = &e->next;
        }
    }
}

/* Block a task until wake_at_ns (CLOCK_MONOTONIC nanoseconds) */
void scheduler_sleep(Scheduler* sched, Task* task, int64_t wake_at_ns) {
    scheduler_add_timer(sched, task, wake_at_ns);
    /* Block with IO tracking so blocked_count is correct and task stays alive */
    scheduler_block_io(sched, task);
}

/* Milliseconds until the earliest timer (rounded up), or -1 if none */
static int next_timer_ms(Scheduler* sched) {
    if (!sched->sleep_list) return -1;
    int64_t earliest = sched->sleep_list->wake_at_ns;
    for (SleepEntry* e = sched->sleep_list->next; e; e = e->next) {
        if (e->wake_at_ns < earliest) earliest = e->wake_at_ns;
    }
    int64_t delta = earliest - now_ns();
    if (delta <= 0) return 0;
    int64_t ms = (delta + 999999) / 1000000;
    return ms > 1000000 ? 1000000 : (int)ms;
}

/* Wake any sleep entries whose deadline has passed */
static void scheduler_check_timers(Scheduler* sched) {
    if (!sched->sleep_list) return;
    int64_t now_ns_v = now_ns();

    SleepEntry** pp = &sched->sleep_list;
    while (*pp) {
        SleepEntry* e = *pp;
        if (now_ns_v >= e->wake_at_ns) {
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

bool scheduler_can_park(VM* vm) {
    return vm->scheduler && vm->scheduler->current && vm->scheduler->io_reactor;
}

void scheduler_park_native(VM* vm, int fd, bool write) {
    Task* task = vm->scheduler->current;
    scheduler_block_io(vm->scheduler, task);
    io_reactor_wait(vm->scheduler->io_reactor, fd, write, task);
    vm->native_blocked = true;
    vm->yielded = true;
}

/* Wake tasks whose fds are ready, without waiting */
static void scheduler_check_io(Scheduler* sched) {
    if (sched->io_reactor && io_reactor_waiting(sched->io_reactor) > 0) {
        io_reactor_poll(sched->io_reactor, sched, 0, -1, NULL);
    }
}

/* Nothing is ready: block in the platform wait until an fd is ready or the
 * next timer is due (or watch_fd is readable, if >= 0). */
static bool scheduler_wait(Scheduler* sched, int watch_fd) {
    int timeout = next_timer_ms(sched);
    if (timeout < 0 && watch_fd < 0 && io_reactor_waiting(sched->io_reactor) == 0) {
        /* Blocked tasks with no fd and no timer can't be woken here */
        timeout = 100;
    }
    bool watch_ready = false;
    io_reactor_poll(sched->io_reactor, sched, timeout, watch_fd, &watch_ready);
    scheduler_check_timers(sched);
    return watch_ready;
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

    /* Spawn a callback task for each watcher (task_new retains the
     * callback and result_map itself), then drop the watcher node's ref. */
    while (w) {
        WatcherNode* next = w->next;
        scheduler_spawn(sched, w->callback, 1, &result_map);
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
    /* A kevent/epoll_wait per tick would be a syscall every quantum */
    if ((++sched->ticks & (IO_CHECK_EVERY - 1)) == 0 || !scheduler_has_ready(sched)) {
        scheduler_check_io(sched);
    }

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
    while (target->state != TASK_DONE) {
        if (scheduler_run_one_tick(sched)) continue;
        if (sched->blocked_count == 0) break;  /* deadlocked on channels */
        scheduler_wait(sched, -1);
    }
    sched->current = saved_current;
}

/* Non-blocking counterpart to scheduler_run_until_done: wakes tasks whose
 * I/O is ready, runs whatever is ready, and returns without waiting on the
 * tasks that stay blocked. Safe to call once per frame from a game loop. */
void scheduler_run_ready(Scheduler* sched) {
    scheduler_check_timers(sched);
    scheduler_check_io(sched);
    while (scheduler_has_ready(sched)) {
        scheduler_run_one_tick(sched);
    }
}

/* Run until no task is ready or blocked on I/O or a timer. Runs forever
 * while a background task (e.g. a server's accept loop) is waiting. */
void scheduler_run_until_done(Scheduler* sched) {
    while (scheduler_has_ready(sched) || sched->blocked_count > 0) {
        if (!scheduler_run_one_tick(sched)) scheduler_wait(sched, -1);
    }
}

void scheduler_run_until_readable(Scheduler* sched, int fd) {
    for (;;) {
        bool ready = false;
        if (scheduler_has_ready(sched)) {
            io_reactor_poll(sched->io_reactor, sched, 0, fd, &ready);
            scheduler_check_timers(sched);
        } else {
            ready = scheduler_wait(sched, fd);
        }
        if (ready) return;
        for (int i = 0; i < IO_CHECK_EVERY && scheduler_has_ready(sched); i++) {
            scheduler_run_one_tick(sched);
        }
    }
}
