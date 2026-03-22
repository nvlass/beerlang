/* Reactor - Platform-neutral async I/O event notification
 *
 * Wraps kqueue (macOS/BSD) or epoll (Linux) behind a simple API.
 * Used by the I/O reactor thread to detect fd readiness.
 */

#ifndef BEERLANG_REACTOR_H
#define BEERLANG_REACTOR_H

#include <stdbool.h>

typedef struct Reactor Reactor;

/* Event returned by reactor_poll */
typedef struct {
    int fd;
    bool readable;
    bool writable;
    void* userdata;
} ReactorEvent;

/* Create a new reactor (kqueue/epoll fd) */
Reactor* reactor_new(void);

/* Destroy reactor */
void reactor_free(Reactor* r);

/* Register interest in a fd. userdata is returned in events.
 * Returns 0 on success, -1 on error. */
int reactor_add(Reactor* r, int fd, bool read, bool write, void* userdata);

/* Remove a fd from the reactor. Returns 0 on success, -1 on error. */
int reactor_remove(Reactor* r, int fd);

/* Poll for events. Returns number of events (0 on timeout, -1 on error).
 * timeout_ms: -1 = block forever, 0 = non-blocking, >0 = milliseconds. */
int reactor_poll(Reactor* r, ReactorEvent* out, int max_events, int timeout_ms);

#endif /* BEERLANG_REACTOR_H */
