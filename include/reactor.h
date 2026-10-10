/* Reactor - Platform-neutral async I/O event notification
 *
 * Wraps kqueue (macOS/BSD) or epoll (Linux) behind a simple API.
 * Level-triggered. A bare-metal port supplies its own implementation
 * of these four functions (polling or an interrupt ring).
 */

#ifndef BEERLANG_REACTOR_H
#define BEERLANG_REACTOR_H

#include <stdbool.h>

typedef struct Reactor Reactor;

/* Event returned by reactor_poll. Hang-up and error conditions report
 * both readable and writable, so every waiter retries and sees them. */
typedef struct {
    int fd;
    bool readable;
    bool writable;
} ReactorEvent;

/* Create a new reactor (kqueue/epoll fd) */
Reactor* reactor_new(void);

/* Destroy reactor */
void reactor_free(Reactor* r);

/* Set the readiness a fd is watched for; read=write=false stops watching it.
 * Returns 0 on success, -1 on error. */
int reactor_set(Reactor* r, int fd, bool read, bool write);

/* Poll for events. Returns number of events (0 on timeout or EINTR, -1 on error).
 * timeout_ms: -1 = block forever, 0 = non-blocking, >0 = milliseconds. */
int reactor_poll(Reactor* r, ReactorEvent* out, int max_events, int timeout_ms);

#endif /* BEERLANG_REACTOR_H */
