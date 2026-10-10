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

/* Read and write are independent kqueue filters. Deleting a filter that
 * isn't registered fails with ENOENT, which is fine. */
static int kq_filter(Reactor* r, int fd, short filter, bool on) {
    struct kevent ch;
    EV_SET(&ch, fd, filter, on ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, NULL);
    if (kevent(r->kq, &ch, 1, NULL, 0, NULL) < 0 && on) return -1;
    return 0;
}

int reactor_set(Reactor* r, int fd, bool read, bool write) {
    int rc = kq_filter(r, fd, EVFILT_READ, read);
    if (kq_filter(r, fd, EVFILT_WRITE, write) < 0) rc = -1;
    return rc;
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
    if (n < 0) return errno == EINTR ? 0 : -1;

    for (int i = 0; i < n; i++) {
        bool failed = (events[i].flags & EV_ERROR) != 0;
        out[i].fd = (int)events[i].ident;
        out[i].readable = failed || events[i].filter == EVFILT_READ;
        out[i].writable = failed || events[i].filter == EVFILT_WRITE;
    }
    return n;
}

#elif defined(__linux__)
/* ================================================================
 * epoll implementation (Linux)
 * ================================================================ */

#include <sys/epoll.h>

struct Reactor {
    int epfd;
};

Reactor* reactor_new(void) {
    int epfd = epoll_create1(0);
    if (epfd < 0) return NULL;

    Reactor* r = malloc(sizeof(Reactor));
    if (!r) { close(epfd); return NULL; }
    r->epfd = epfd;
    return r;
}

void reactor_free(Reactor* r) {
    if (!r) return;
    close(r->epfd);
    free(r);
}

int reactor_set(Reactor* r, int fd, bool read, bool write) {
    if (!read && !write) {
        epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
        return 0;
    }
    struct epoll_event ev = {0};
    if (read) ev.events |= EPOLLIN;
    if (write) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    if (epoll_ctl(r->epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        if (epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) return -1;
    }
    return 0;
}

int reactor_poll(Reactor* r, ReactorEvent* out, int max_events, int timeout_ms) {
    struct epoll_event events[max_events];

    int n = epoll_wait(r->epfd, events, max_events, timeout_ms);
    if (n < 0) return errno == EINTR ? 0 : -1;

    for (int i = 0; i < n; i++) {
        bool failed = (events[i].events & (EPOLLHUP | EPOLLERR)) != 0;
        out[i].fd = events[i].data.fd;
        out[i].readable = failed || (events[i].events & EPOLLIN) != 0;
        out[i].writable = failed || (events[i].events & EPOLLOUT) != 0;
    }
    return n;
}

#else
#error "Unsupported platform: need kqueue (macOS/BSD) or epoll (Linux)"
#endif
