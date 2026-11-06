/* Reactor - Platform-neutral async I/O event notification
 *
 * Wraps kqueue (macOS/BSD) or epoll (Linux) behind a simple API.
 * Level-triggered.
 *
 * With BEER_PLATFORM=embedder the embedder supplies these four functions
 * (src/io/reactor.c isn't built) and the scheduler parks tasks on them
 * through io_reactor.c as usual. The contract:
 *
 * - "fds" are whatever non-negative ints the embedder's natives pass to
 *   scheduler_park_native: real fds, or e.g. one per IRQ. io_reactor keeps
 *   an array indexed by fd, so keep the numbers small.
 * - Level-triggered: reactor_poll reports a watched fd for as long as its
 *   condition holds, and must not consume it (e.g. clear an IRQ's pending
 *   bit). A woken task runs its native again from the start, so the native
 *   checks the condition, consumes it itself, and parks again otherwise;
 *   a poll that cleared it would lose the wakeup. A condition that is true
 *   before the task parks is then reported by the next poll, so none is
 *   lost between the native's check and its park either.
 * - Only fds with interest set (reactor_set) are reported.
 * - A wait (timeout_ms != 0) may return 0 early, and should return after
 *   any interrupt even if no watched fd became ready (as kqueue/epoll do on
 *   EINTR): the scheduler loops, and checks timers, Ctrl-C
 *   (vm_request_interrupt) and task interrupts in between. Waiting for a
 *   watched fd in a loop instead would miss those. A bare-metal port can
 *   wfi once per call.
 * - Called only from the scheduler's thread, never from an interrupt
 *   handler: a handler only records what happened (sets a bit).
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
