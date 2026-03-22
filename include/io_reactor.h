/* IOReactor - Threaded I/O reactor with completion queue
 *
 * Runs a reactor_poll loop on a dedicated pthread. When fds become
 * ready, pushes woken Task pointers into a mutex-protected ring buffer.
 * The scheduler drains completions on each tick.
 */

#ifndef BEERLANG_IO_REACTOR_H
#define BEERLANG_IO_REACTOR_H

#include <stdbool.h>

/* Forward declarations */
typedef struct Task Task;

typedef struct IOReactor IOReactor;

/* Create reactor and start background thread. Returns NULL on failure. */
IOReactor* io_reactor_new(void);

/* Stop thread, join, free resources. */
void io_reactor_free(IOReactor* r);

/* Register a fd for monitoring. When it becomes ready, the given task
 * will appear in io_reactor_drain(). Thread-safe. */
void io_reactor_register(IOReactor* r, int fd, bool read, bool write, Task* task);

/* Unregister a fd. Thread-safe. */
void io_reactor_unregister(IOReactor* r, int fd);

/* Drain completed tasks into caller's array. Returns count of tasks.
 * Non-blocking. Thread-safe. */
int io_reactor_drain(IOReactor* r, Task** tasks_out, int max);

#endif /* BEERLANG_IO_REACTOR_H */
