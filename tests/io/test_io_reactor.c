/* Tests for the threaded I/O reactor */

#include "test.h"
#include "io_reactor.h"
#include "task.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* Test creation and destruction */
TEST(io_reactor_create_free) {
    IOReactor* r = io_reactor_new();
    ASSERT(r != NULL, "io_reactor_new should succeed");
    io_reactor_free(r);
    return NULL;
}

/* Test: drain with nothing registered returns 0 */
TEST(io_reactor_drain_empty) {
    IOReactor* r = io_reactor_new();
    Task* tasks[8];
    int n = io_reactor_drain(r, tasks, 8);
    ASSERT(n == 0, "drain with no registrations should return 0");
    io_reactor_free(r);
    return NULL;
}

/* Test: register pipe fd, write to it, drain returns the task pointer */
TEST(io_reactor_pipe_wakeup) {
    IOReactor* r = io_reactor_new();

    int pipefd[2];
    ASSERT(pipe(pipefd) == 0, "pipe should succeed");

    /* Use a sentinel as a fake Task pointer */
    int sentinel = 42;
    Task* fake_task = (Task*)(uintptr_t)&sentinel;

    io_reactor_register(r, pipefd[0], true, false, fake_task);

    /* Write data to make read end readable */
    write(pipefd[1], "hello", 5);

    /* Give reactor thread time to poll */
    usleep(30000);  /* 30ms */

    Task* tasks[8];
    int n = io_reactor_drain(r, tasks, 8);
    ASSERT(n == 1, "should drain 1 completion");
    ASSERT(tasks[0] == fake_task, "drained task should match");

    /* Second drain should be empty (reactor removed the fd) */
    n = io_reactor_drain(r, tasks, 8);
    ASSERT(n == 0, "second drain should be empty");

    close(pipefd[0]);
    close(pipefd[1]);
    io_reactor_free(r);
    return NULL;
}

static const char* all_tests(void) {
    RUN_TEST(io_reactor_create_free);
    RUN_TEST(io_reactor_drain_empty);
    RUN_TEST(io_reactor_pipe_wakeup);
    return NULL;
}

int main(void) {
    printf("Testing I/O reactor thread...\n");
    RUN_SUITE(all_tests);
    return 0;
}
