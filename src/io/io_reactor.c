/* IOReactor - Threaded I/O reactor with completion queue */

#include "io_reactor.h"
#include "reactor.h"
#include "task.h"
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#define COMPLETION_RING_SIZE 256
#define MAX_POLL_EVENTS 32

struct IOReactor {
    Reactor* reactor;
    pthread_t thread;
    pthread_mutex_t mutex;
    bool running;

    /* Completion ring buffer (mutex-protected) */
    Task* completions[COMPLETION_RING_SIZE];
    int ring_head;  /* next read position */
    int ring_tail;  /* next write position */
};

static void* io_reactor_thread(void* arg) {
    IOReactor* r = (IOReactor*)arg;
    ReactorEvent events[MAX_POLL_EVENTS];

    while (r->running) {
        int n = reactor_poll(r->reactor, events, MAX_POLL_EVENTS, 10);
        if (n <= 0) continue;

        pthread_mutex_lock(&r->mutex);
        for (int i = 0; i < n; i++) {
            Task* task = (Task*)events[i].userdata;
            if (!task) continue;

            /* Remove fd from reactor so it doesn't fire again */
            reactor_remove(r->reactor, events[i].fd);

            /* Push to completion ring */
            int next_tail = (r->ring_tail + 1) % COMPLETION_RING_SIZE;
            if (next_tail != r->ring_head) {  /* not full */
                r->completions[r->ring_tail] = task;
                r->ring_tail = next_tail;
            } else {
                fprintf(stderr, "[io_reactor] WARNING: completion ring full, task=%p lost!\n", (void*)task);
            }
        }
        pthread_mutex_unlock(&r->mutex);
    }

    return NULL;
}

IOReactor* io_reactor_new(void) {
    IOReactor* r = calloc(1, sizeof(IOReactor));
    if (!r) return NULL;

    r->reactor = reactor_new();
    if (!r->reactor) { free(r); return NULL; }

    pthread_mutex_init(&r->mutex, NULL);
    r->ring_head = 0;
    r->ring_tail = 0;
    r->running = true;

    if (pthread_create(&r->thread, NULL, io_reactor_thread, r) != 0) {
        reactor_free(r->reactor);
        pthread_mutex_destroy(&r->mutex);
        free(r);
        return NULL;
    }

    return r;
}

void io_reactor_free(IOReactor* r) {
    if (!r) return;
    r->running = false;
    pthread_join(r->thread, NULL);
    reactor_free(r->reactor);
    pthread_mutex_destroy(&r->mutex);
    free(r);
}

void io_reactor_register(IOReactor* r, int fd, bool read, bool write, Task* task) {
    pthread_mutex_lock(&r->mutex);
    reactor_add(r->reactor, fd, read, write, task);
    pthread_mutex_unlock(&r->mutex);
}

void io_reactor_unregister(IOReactor* r, int fd) {
    pthread_mutex_lock(&r->mutex);
    reactor_remove(r->reactor, fd);
    pthread_mutex_unlock(&r->mutex);
}

int io_reactor_drain(IOReactor* r, Task** tasks_out, int max) {
    pthread_mutex_lock(&r->mutex);
    int count = 0;
    while (r->ring_head != r->ring_tail && count < max) {
        tasks_out[count++] = r->completions[r->ring_head];
        r->ring_head = (r->ring_head + 1) % COMPLETION_RING_SIZE;
    }
    pthread_mutex_unlock(&r->mutex);
    return count;
}
