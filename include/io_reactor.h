/* IOReactor - tasks waiting on fd readiness
 *
 * Per-fd lists of tasks waiting to read or write, on top of the platform
 * reactor (reactor.h). Single-threaded: the scheduler polls it from its
 * own loop, blocking in the platform wait only when no task is ready.
 * Waiting is one-shot: a woken task retries its operation and waits again
 * if it still can't make progress.
 */

#ifndef BEERLANG_IO_REACTOR_H
#define BEERLANG_IO_REACTOR_H

#include <stdbool.h>

typedef struct Task Task;
typedef struct Scheduler Scheduler;
typedef struct IOReactor IOReactor;

/* Returns NULL on failure. */
IOReactor* io_reactor_new(void);

void io_reactor_free(IOReactor* r);

/* Wake `task` (via scheduler_wake_io) when fd becomes readable, or writable
 * if `write`. The caller has already blocked the task with scheduler_block_io. */
void io_reactor_wait(IOReactor* r, int fd, bool write, Task* task);

/* Wake every task waiting on fd and stop watching it. Call before closing fd. */
void io_reactor_cancel_fd(IOReactor* r, Scheduler* sched, int fd);

/* Drop `task` from all wait lists without waking it (it was woken another
 * way, e.g. by a timeout). */
void io_reactor_forget(IOReactor* r, Task* task);

/* Number of tasks currently waiting on fds. */
int io_reactor_waiting(IOReactor* r);

/* Wait up to timeout_ms (-1 = forever, 0 = just check) for fd readiness and
 * wake the waiting tasks. If watch_fd >= 0 it is also watched for reading
 * and *watch_ready reports whether it is readable. Returns tasks woken. */
int io_reactor_poll(IOReactor* r, Scheduler* sched, int timeout_ms,
                    int watch_fd, bool* watch_ready);

#endif /* BEERLANG_IO_REACTOR_H */
