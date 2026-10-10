/* Tests for the I/O reactor: per-fd task wait lists polled by the scheduler */

#include "test.h"
#include "io_reactor.h"
#include "scheduler.h"
#include "task.h"
#include "memory.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>

/* A task object that only needs to be blockable and wakeable */
static Task* blocked_task(Scheduler* sched) {
    Task* t = (Task*)object_alloc(TYPE_TASK, sizeof(Task));
    memset((char*)t + sizeof(struct Object), 0, sizeof(Task) - sizeof(struct Object));
    object_make_immortal(tag_pointer(t));
    scheduler_block_io(sched, t);
    return t;
}

TEST(io_reactor_create_free) {
    IOReactor* r = io_reactor_new();
    ASSERT(r != NULL, "io_reactor_new should succeed");
    io_reactor_free(r);
    return NULL;
}

TEST(io_reactor_poll_empty) {
    Scheduler* sched = scheduler_new(0);
    int n = io_reactor_poll(sched->io_reactor, sched, 0, -1, NULL);
    ASSERT(n == 0, "poll with no waiters should wake nothing");
    scheduler_free(sched);
    return NULL;
}

/* A waiting reader is woken once its fd is readable, and only once */
TEST(io_reactor_pipe_wakeup) {
    Scheduler* sched = scheduler_new(0);
    IOReactor* r = sched->io_reactor;
    int pipefd[2];
    ASSERT(pipe(pipefd) == 0, "pipe should succeed");

    Task* t = blocked_task(sched);
    io_reactor_wait(r, pipefd[0], false, t);
    ASSERT(io_reactor_waiting(r) == 1, "one task waiting");
    ASSERT(io_reactor_poll(r, sched, 0, -1, NULL) == 0, "nothing ready yet");

    write(pipefd[1], "hello", 5);
    ASSERT(io_reactor_poll(r, sched, 100, -1, NULL) == 1, "reader woken");
    ASSERT(t->state == TASK_READY, "woken task is ready");
    ASSERT(io_reactor_waiting(r) == 0, "no task waiting");
    ASSERT(io_reactor_poll(r, sched, 0, -1, NULL) == 0, "waits are one-shot");

    close(pipefd[0]);
    close(pipefd[1]);
    scheduler_free(sched);
    return NULL;
}

/* A reader and a writer on one socket are woken independently */
TEST(io_reactor_reader_and_writer) {
    Scheduler* sched = scheduler_new(0);
    IOReactor* r = sched->io_reactor;
    int sv[2];
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair should succeed");

    Task* reader = blocked_task(sched);
    Task* writer = blocked_task(sched);
    io_reactor_wait(r, sv[0], false, reader);
    io_reactor_wait(r, sv[0], true, writer);

    ASSERT(io_reactor_poll(r, sched, 100, -1, NULL) == 1, "only the writer is ready");
    ASSERT(writer->state == TASK_READY, "writer woken");
    ASSERT(reader->state == TASK_BLOCKED, "reader still waiting");
    ASSERT(io_reactor_waiting(r) == 1, "reader still registered");

    write(sv[1], "x", 1);
    ASSERT(io_reactor_poll(r, sched, 100, -1, NULL) == 1, "reader woken by data");
    ASSERT(reader->state == TASK_READY, "reader woken");

    close(sv[0]);
    close(sv[1]);
    scheduler_free(sched);
    return NULL;
}

/* Closing an fd wakes every task waiting on it */
TEST(io_reactor_cancel_fd) {
    Scheduler* sched = scheduler_new(0);
    IOReactor* r = sched->io_reactor;
    int pipefd[2];
    pipe(pipefd);

    Task* a = blocked_task(sched);
    Task* b = blocked_task(sched);
    io_reactor_wait(r, pipefd[0], false, a);
    io_reactor_wait(r, pipefd[0], false, b);
    io_reactor_cancel_fd(r, sched, pipefd[0]);
    ASSERT(a->state == TASK_READY && b->state == TASK_READY, "both waiters woken");
    ASSERT(io_reactor_waiting(r) == 0, "no task waiting");

    write(pipefd[1], "x", 1);
    ASSERT(io_reactor_poll(r, sched, 0, -1, NULL) == 0, "fd no longer watched");

    close(pipefd[0]);
    close(pipefd[1]);
    scheduler_free(sched);
    return NULL;
}

/* forget drops a task's waits without waking it */
TEST(io_reactor_forget) {
    Scheduler* sched = scheduler_new(0);
    IOReactor* r = sched->io_reactor;
    int pipefd[2];
    pipe(pipefd);

    Task* t = blocked_task(sched);
    io_reactor_wait(r, pipefd[0], false, t);
    io_reactor_forget(r, t);
    ASSERT(io_reactor_waiting(r) == 0, "no task waiting");

    write(pipefd[1], "x", 1);
    ASSERT(io_reactor_poll(r, sched, 0, -1, NULL) == 0, "forgotten task not woken");
    ASSERT(t->state == TASK_BLOCKED, "task still blocked");

    close(pipefd[0]);
    close(pipefd[1]);
    scheduler_free(sched);
    return NULL;
}

/* watch_fd reports readiness without waking anyone or staying watched */
TEST(io_reactor_watch_fd) {
    Scheduler* sched = scheduler_new(0);
    IOReactor* r = sched->io_reactor;
    int pipefd[2];
    pipe(pipefd);

    bool ready = true;
    io_reactor_poll(r, sched, 0, pipefd[0], &ready);
    ASSERT(!ready, "empty pipe not readable");

    write(pipefd[1], "x", 1);
    io_reactor_poll(r, sched, 100, pipefd[0], &ready);
    ASSERT(ready, "pipe with data readable");
    ASSERT(io_reactor_poll(r, sched, 0, -1, NULL) == 0, "watch fd not left registered");

    close(pipefd[0]);
    close(pipefd[1]);
    scheduler_free(sched);
    return NULL;
}

static const char* all_tests(void) {
    RUN_TEST(io_reactor_create_free);
    RUN_TEST(io_reactor_poll_empty);
    RUN_TEST(io_reactor_pipe_wakeup);
    RUN_TEST(io_reactor_reader_and_writer);
    RUN_TEST(io_reactor_cancel_fd);
    RUN_TEST(io_reactor_forget);
    RUN_TEST(io_reactor_watch_fd);
    return NULL;
}

int main(void) {
    printf("Testing io_reactor...\n");
    RUN_SUITE(all_tests);
    return 0;
}
