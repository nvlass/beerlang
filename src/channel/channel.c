/* Channel implementation - CSP-style message passing */

#include <stdlib.h>
#include <string.h>
#include "channel.h"
#include "scheduler.h"
#include "vm.h"
#include "memory.h"

/* Channel destructor */
static void channel_destroy(struct Object* obj) {
    Channel* ch = (Channel*)obj;
    /* Release buffered values */
    for (int i = 0; i < ch->count; i++) {
        int idx = (ch->head + i) % ch->capacity;
        if (is_pointer(ch->buffer[idx])) {
            object_release(ch->buffer[idx]);
        }
    }
    free(ch->buffer);
    /* Release any tasks still waiting in queues */
    while (ch->send_head.next) {
        Task* t = ch->send_head.next;
        ch->send_head.next = t->next;
        Value tv = tag_pointer(t);
        object_release(tv);
    }
    while (ch->recv_head.next) {
        Task* t = ch->recv_head.next;
        ch->recv_head.next = t->next;
        Value tv = tag_pointer(t);
        object_release(tv);
    }
}

/* Initialize channel type */
void channel_init(void) {
    object_register_destructor(TYPE_CHANNEL, channel_destroy);
}

/* Create a new channel */
Value channel_new(int capacity) {
    size_t size = sizeof(Channel);
    Channel* ch = (Channel*)object_alloc(TYPE_CHANNEL, size);
    Value ch_val = tag_pointer(ch);

    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = false;

    if (capacity > 0) {
        ch->buffer = malloc(sizeof(Value) * (size_t)capacity);
    } else {
        ch->buffer = NULL;
    }

    /* Initialize waiting queue sentinels */
    ch->send_head.next = NULL;
    ch->send_head.prev = NULL;
    ch->recv_head.next = NULL;
    ch->recv_head.prev = NULL;

    return ch_val;
}

/* Helper: append task to a waiting queue (retains task) */
static void queue_push(Task* sentinel, Task* task) {
    Value task_val = tag_pointer(task);
    object_retain(task_val);
    task->next = NULL;
    if (!sentinel->next) {
        sentinel->next = task;
        task->prev = NULL;
    } else {
        Task* tail = sentinel->next;
        while (tail->next) tail = tail->next;
        tail->next = task;
        task->prev = tail;
    }
}

/* Helper: pop first task from a waiting queue (transfers ownership to caller) */
static Task* queue_pop(Task* sentinel) {
    Task* task = sentinel->next;
    if (!task) return NULL;
    sentinel->next = task->next;
    if (task->next) task->next->prev = NULL;
    task->next = NULL;
    task->prev = NULL;
    /* Caller takes ownership of the retain from queue_push */
    return task;
}

/* Helper: check if queue is empty */
static bool queue_empty(Task* sentinel) {
    return sentinel->next == NULL;
}

/* Send a value on the channel */
bool channel_send(Value ch_val, Value val, Scheduler* sched, Task* task) {
    Channel* ch = channel_get(ch_val);

    if (ch->closed) {
        /* Sending on closed channel — just return true (value is dropped) */
        return true;
    }

    /* Check for waiting receiver */
    if (!queue_empty(&ch->recv_head)) {
        Task* receiver = queue_pop(&ch->recv_head);
        /* Transfer value directly: push onto receiver's VM stack */
        VM* rvm = receiver->vm;
        /* Pop channel from receiver's stack */
        vm_pop(rvm);
        /* Push received value */
        vm_push(rvm, val);
        /* Advance receiver's PC past the OP_CHAN_RECV opcode (1 byte) */
        rvm->pc++;

        scheduler_wake(sched, receiver);
        Value rv = tag_pointer(receiver);
        object_release(rv);
        return true;
    }

    /* Buffered channel with room */
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = val;
        if (is_pointer(val)) object_retain(val);
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return true;
    }

    /* Must block sender */
    if (task) {
        task->blocked_value = val;
        if (is_pointer(val)) object_retain(val);
        scheduler_block(sched, task);
        queue_push(&ch->send_head, task);
    }
    return false;
}

/* Receive a value from the channel */
bool channel_recv(Value ch_val, Scheduler* sched, Task* task, Value* out) {
    Channel* ch = channel_get(ch_val);

    /* Check for waiting sender (rendezvous or buffer full) */
    if (!queue_empty(&ch->send_head)) {
        Task* sender = queue_pop(&ch->send_head);
        Value val;

        if (ch->capacity > 0 && ch->count > 0) {
            /* Buffered: take from buffer, move sender's value into buffer */
            val = ch->buffer[ch->head];
            ch->head = (ch->head + 1) % ch->capacity;

            /* Enqueue sender's value */
            ch->buffer[ch->tail] = sender->blocked_value;
            ch->tail = (ch->tail + 1) % ch->capacity;
            /* count stays the same */
        } else {
            /* Rendezvous: direct transfer from sender */
            val = sender->blocked_value;
        }

        sender->blocked_value = VALUE_NIL;

        /* Advance sender's VM past the blocked OP_CHAN_SEND.
         * The sender's PC points at the OP_CHAN_SEND opcode.
         * We need to pop val+ch from sender's stack and push true. */
        VM* svm = sender->vm;
        vm_pop(svm);  /* pop val */
        vm_pop(svm);  /* pop channel */
        vm_push(svm, VALUE_TRUE);
        svm->pc++;  /* skip OP_CHAN_SEND opcode byte */

        scheduler_wake(sched, sender);
        Value sv = tag_pointer(sender);
        object_release(sv);
        *out = val;
        return true;
    }

    /* Buffered channel with data */
    if (ch->capacity > 0 && ch->count > 0) {
        *out = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        return true;
    }

    /* Closed empty channel → return nil */
    if (ch->closed) {
        *out = VALUE_NIL;
        return true;
    }

    /* Must block receiver */
    if (task) {
        scheduler_block(sched, task);
        queue_push(&ch->recv_head, task);
    }
    return false;
}

/* Close the channel, wake all blocked tasks */
void channel_close(Value ch_val, Scheduler* sched) {
    Channel* ch = channel_get(ch_val);
    ch->closed = true;

    /* Wake all blocked receivers (they'll get nil) */
    while (!queue_empty(&ch->recv_head)) {
        Task* receiver = queue_pop(&ch->recv_head);
        VM* rvm = receiver->vm;
        /* Pop channel, push nil */
        vm_pop(rvm);
        vm_push(rvm, VALUE_NIL);
        rvm->pc++;  /* skip OP_CHAN_RECV */
        scheduler_wake(sched, receiver);
        Value rv = tag_pointer(receiver);
        object_release(rv);
    }

    /* Wake all blocked senders (their sends are dropped) */
    while (!queue_empty(&ch->send_head)) {
        Task* sender = queue_pop(&ch->send_head);
        if (is_pointer(sender->blocked_value)) {
            object_release(sender->blocked_value);
        }
        sender->blocked_value = VALUE_NIL;
        VM* svm = sender->vm;
        vm_pop(svm);  /* pop val */
        vm_pop(svm);  /* pop channel */
        vm_push(svm, VALUE_NIL);  /* send returns nil on closed */
        svm->pc++;  /* skip OP_CHAN_SEND */
        scheduler_wake(sched, sender);
        Value sv = tag_pointer(sender);
        object_release(sv);
    }
}
