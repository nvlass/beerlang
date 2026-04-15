/* Reactor - Platform-specific async I/O event notification
 *
 * kqueue on macOS/BSD, epoll on Linux.
 */

#include "reactor.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#ifdef __APPLE__
/* ================================================================
 * kqueue implementation (macOS / BSD)
 * ================================================================ */

#include <sys/event.h>
#include <sys/time.h>

struct Reactor {
    int kq;
};

Reactor* reactor_new(void) {
    int kq = kqueue();
    if (kq < 0) return NULL;

    Reactor* r = malloc(sizeof(Reactor));
    if (!r) { close(kq); return NULL; }
    r->kq = kq;
    return r;
}

void reactor_free(Reactor* r) {
    if (!r) return;
    close(r->kq);
    free(r);
}

int reactor_add(Reactor* r, int fd, bool read, bool write, void* userdata) {
    struct kevent changes[2];
    int nchanges = 0;

    if (read) {
        EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, userdata);
        nchanges++;
    }
    if (write) {
        EV_SET(&changes[nchanges], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, userdata);
        nchanges++;
    }
    if (nchanges == 0) return -1;

    int ret = kevent(r->kq, changes, nchanges, NULL, 0, NULL);
    return ret < 0 ? -1 : 0;
}

int reactor_remove(Reactor* r, int fd) {
    struct kevent changes[2];
    int nchanges = 0;

    /* Try removing both filters — ignore errors (filter may not exist) */
    EV_SET(&changes[nchanges++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[nchanges++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(r->kq, changes, nchanges, NULL, 0, NULL);
    return 0;
}

int reactor_poll(Reactor* r, ReactorEvent* out, int max_events, int timeout_ms) {
    struct kevent events[max_events];
    struct timespec ts;
    struct timespec* tsp = NULL;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }

    int n = kevent(r->kq, NULL, 0, events, max_events, tsp);
    if (n < 0) return -1;

    for (int i = 0; i < n; i++) {
        out[i].fd = (int)events[i].ident;
        out[i].readable = (events[i].filter == EVFILT_READ);
        out[i].writable = (events[i].filter == EVFILT_WRITE);
        out[i].userdata = events[i].udata;
    }
    return n;
}

#elif defined(__linux__)
/* ================================================================
 * epoll implementation (Linux)
 * ================================================================ */

#include <sys/epoll.h>

/* We need to store userdata per-fd since epoll doesn't have a udata field
 * like kqueue. We use a simple array indexed by fd. */
#define MAX_FDS 4096

struct Reactor {
    int epfd;
    void* userdata[MAX_FDS];
};

Reactor* reactor_new(void) {
    int epfd = epoll_create1(0);
    if (epfd < 0) return NULL;

    Reactor* r = calloc(1, sizeof(Reactor));
    if (!r) { close(epfd); return NULL; }
    r->epfd = epfd;
    return r;
}

void reactor_free(Reactor* r) {
    if (!r) return;
    close(r->epfd);
    free(r);
}

int reactor_add(Reactor* r, int fd, bool read, bool write, void* userdata) {
    if (fd < 0 || fd >= MAX_FDS) return -1;

    struct epoll_event ev = {0};
    if (read) ev.events |= EPOLLIN;
    if (write) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    r->userdata[fd] = userdata;

    /* Try EPOLL_CTL_MOD first (in case already added), fall back to ADD */
    if (epoll_ctl(r->epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        if (epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) return -1;
    }
    return 0;
}

int reactor_remove(Reactor* r, int fd) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    r->userdata[fd] = NULL;
    epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
    return 0;
}

int reactor_poll(Reactor* r, ReactorEvent* out, int max_events, int timeout_ms) {
    struct epoll_event events[max_events];

    int n = epoll_wait(r->epfd, events, max_events, timeout_ms);
    if (n < 0) return -1;

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        out[i].fd = fd;
        out[i].readable = (events[i].events & EPOLLIN) != 0;
        out[i].writable = (events[i].events & EPOLLOUT) != 0;
        out[i].userdata = (fd >= 0 && fd < MAX_FDS) ? r->userdata[fd] : NULL;
    }
    return n;
}

#else
#error "Unsupported platform: need kqueue (macOS/BSD) or epoll (Linux)"
#endif
