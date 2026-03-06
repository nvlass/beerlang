/* Channel - CSP-style communication between tasks
 *
 * Channels provide synchronous (rendezvous) or buffered message passing.
 * Operations may block the calling task until a partner is available.
 */

#ifndef BEERLANG_CHANNEL_H
#define BEERLANG_CHANNEL_H

#include "value.h"
#include "task.h"

/* Forward declaration */
typedef struct Scheduler Scheduler;

/* Channel object structure */
typedef struct Channel {
    struct Object header;

    /* Circular buffer */
    Value* buffer;
    int capacity;       /* 0 = rendezvous (unbuffered) */
    int head;
    int tail;
    int count;

    /* Waiting queues (doubly-linked with sentinels) */
    Task send_head;     /* Senders blocked waiting to send */
    Task recv_head;     /* Receivers blocked waiting to receive */

    bool closed;
} Channel;

/* Create a new channel. capacity=0 for unbuffered (rendezvous). */
Value channel_new(int capacity);

/* Send a value on the channel.
 * Returns true if sent immediately, false if task was blocked.
 * If blocked, task is moved to channel's send queue. */
bool channel_send(Value ch_val, Value val, Scheduler* sched, Task* task);

/* Receive a value from the channel.
 * Returns true if received immediately (value stored in *out),
 * false if task was blocked. */
bool channel_recv(Value ch_val, Scheduler* sched, Task* task, Value* out);

/* Close the channel. Wakes all blocked tasks. */
void channel_close(Value ch_val, Scheduler* sched);

/* Initialize channel type (register destructor) */
void channel_init(void);

/* Type check */
static inline bool is_channel(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_CHANNEL;
}

/* Accessor */
static inline Channel* channel_get(Value v) {
    return (Channel*)untag_pointer(v);
}

#endif /* BEERLANG_CHANNEL_H */
