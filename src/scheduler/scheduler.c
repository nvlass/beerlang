/* Scheduler implementation - cooperative round-robin task scheduler */

#include <stdlib.h>
#include <stdio.h>
#include "scheduler.h"
#include "task.h"
#include "vm.h"
#include "memory.h"

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
    sched->quota = quota > 0 ? quota : DEFAULT_TASK_QUOTA;

    return sched;
}

/* Free scheduler */
void scheduler_free(Scheduler* sched) {
    if (!sched) return;
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

/* Run one task for one quantum */
bool scheduler_run_one_tick(Scheduler* sched) {
    Task* task = scheduler_dequeue(sched);
    if (!task) return false;

    sched->current = task;
    task_run(task);
    sched->current = NULL;

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

/* Run all tasks until ready queue is empty */
void scheduler_run_until_done(Scheduler* sched) {
    int iterations = 0;
    while (scheduler_has_ready(sched)) {
        scheduler_run_one_tick(sched);
        iterations++;
        if (iterations > 1000000) {
            fprintf(stderr, "scheduler: iteration limit reached, ready_count=%d\n", sched->ready_count);
            break;
        }
    }
}
