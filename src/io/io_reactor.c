/* IOReactor - tasks waiting on fd readiness, polled by the scheduler */

#include "io_reactor.h"
#include "reactor.h"
#include "scheduler.h"
#include "task.h"
#include <stdlib.h>
#include <string.h>

#define MAX_POLL_EVENTS 64

typedef struct {
    Task** tasks;
    int n, cap;
} WaitList;

typedef struct {
    WaitList readers;
    WaitList writers;
    bool watch_read, watch_write;   /* interest currently set in the reactor */
} FdWaiters;

struct IOReactor {
    Reactor* reactor;
    FdWaiters* fds;     /* indexed by fd */
    int nfds;
    int waiting;        /* total tasks across all lists */
};

IOReactor* io_reactor_new(void) {
    IOReactor* r = calloc(1, sizeof(IOReactor));
    if (!r) return NULL;
    r->reactor = reactor_new();
    if (!r->reactor) { free(r); return NULL; }
    return r;
}

void io_reactor_free(IOReactor* r) {
    if (!r) return;
    for (int i = 0; i < r->nfds; i++) {
        free(r->fds[i].readers.tasks);
        free(r->fds[i].writers.tasks);
    }
    free(r->fds);
    reactor_free(r->reactor);
    free(r);
}

static FdWaiters* fd_entry(IOReactor* r, int fd) {
    if (fd < 0) return NULL;
    if (fd >= r->nfds) {
        int n = r->nfds ? r->nfds : 16;
        while (n <= fd) n *= 2;
        FdWaiters* grown = realloc(r->fds, (size_t)n * sizeof(FdWaiters));
        if (!grown) return NULL;
        memset(grown + r->nfds, 0, (size_t)(n - r->nfds) * sizeof(FdWaiters));
        r->fds = grown;
        r->nfds = n;
    }
    return &r->fds[fd];
}

/* Bring the reactor's interest for fd in line with its wait lists. */
static void sync_interest(IOReactor* r, int fd) {
    FdWaiters* e = &r->fds[fd];
    bool rd = e->readers.n > 0, wr = e->writers.n > 0;
    if (rd == e->watch_read && wr == e->watch_write) return;
    reactor_set(r->reactor, fd, rd, wr);
    e->watch_read = rd;
    e->watch_write = wr;
}

void io_reactor_wait(IOReactor* r, int fd, bool write, Task* task) {
    FdWaiters* e = fd_entry(r, fd);
    if (!e) return;
    WaitList* l = write ? &e->writers : &e->readers;
    for (int i = 0; i < l->n; i++) {
        if (l->tasks[i] == task) return;
    }
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 2;
        Task** grown = realloc(l->tasks, (size_t)cap * sizeof(Task*));
        if (!grown) return;
        l->tasks = grown;
        l->cap = cap;
    }
    l->tasks[l->n++] = task;
    r->waiting++;
    sync_interest(r, fd);
}

/* Empties the list before waking, so the woken tasks can wait again. */
static int wake_list(IOReactor* r, Scheduler* sched, WaitList* l) {
    int n = l->n;
    Task* woken[n > 0 ? n : 1];
    memcpy(woken, l->tasks, (size_t)n * sizeof(Task*));
    l->n = 0;
    r->waiting -= n;
    for (int i = 0; i < n; i++) scheduler_wake_io(sched, woken[i]);
    return n;
}

void io_reactor_cancel_fd(IOReactor* r, Scheduler* sched, int fd) {
    if (fd < 0 || fd >= r->nfds) return;
    FdWaiters* e = &r->fds[fd];
    wake_list(r, sched, &e->readers);
    wake_list(r, sched, &e->writers);
    sync_interest(r, fd);
}

static void drop_task(WaitList* l, Task* task, int* waiting) {
    for (int i = 0; i < l->n; i++) {
        if (l->tasks[i] == task) {
            l->tasks[i] = l->tasks[--l->n];
            (*waiting)--;
            return;
        }
    }
}

void io_reactor_forget(IOReactor* r, Task* task) {
    for (int fd = 0; fd < r->nfds && r->waiting > 0; fd++) {
        FdWaiters* e = &r->fds[fd];
        if (e->readers.n == 0 && e->writers.n == 0) continue;
        drop_task(&e->readers, task, &r->waiting);
        drop_task(&e->writers, task, &r->waiting);
        sync_interest(r, fd);
    }
}

int io_reactor_waiting(IOReactor* r) {
    return r->waiting;
}

int io_reactor_poll(IOReactor* r, Scheduler* sched, int timeout_ms,
                    int watch_fd, bool* watch_ready) {
    if (watch_ready) *watch_ready = false;
    bool watching = watch_fd >= 0 && fd_entry(r, watch_fd) != NULL;
    if (watching) {
        FdWaiters* e = &r->fds[watch_fd];
        reactor_set(r->reactor, watch_fd, true, e->writers.n > 0);
    }

    ReactorEvent events[MAX_POLL_EVENTS];
    int n = reactor_poll(r->reactor, events, MAX_POLL_EVENTS, timeout_ms);

    if (watching) {
        /* Force a re-sync: the reactor's interest no longer matches the flags */
        FdWaiters* e = &r->fds[watch_fd];
        e->watch_read = true;
        e->watch_write = e->writers.n > 0;
        sync_interest(r, watch_fd);
    }

    int woken = 0;
    for (int i = 0; i < n; i++) {
        int fd = events[i].fd;
        if (fd == watch_fd && events[i].readable && watch_ready) *watch_ready = true;
        if (fd < 0 || fd >= r->nfds) continue;
        FdWaiters* e = &r->fds[fd];
        if (events[i].readable) woken += wake_list(r, sched, &e->readers);
        if (events[i].writable) woken += wake_list(r, sched, &e->writers);
        sync_interest(r, fd);
    }
    return woken;
}
