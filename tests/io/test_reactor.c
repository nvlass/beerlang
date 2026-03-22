/* Tests for the platform reactor (kqueue/epoll) */

#include "test.h"
#include "reactor.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

/* Test reactor creation and destruction */
TEST(reactor_create_free) {
    Reactor* r = reactor_new();
    ASSERT(r != NULL, "reactor_new should succeed");
    reactor_free(r);
    return NULL;
}

/* Test: poll with no registered fds returns 0 on timeout */
TEST(reactor_poll_timeout) {
    Reactor* r = reactor_new();
    ASSERT(r != NULL, "reactor_new should succeed");

    ReactorEvent events[4];
    int n = reactor_poll(r, events, 4, 0);  /* non-blocking */
    ASSERT(n == 0, "poll with no fds should return 0");

    reactor_free(r);
    return NULL;
}

/* Test: add pipe, write to it, poll detects readable */
TEST(reactor_pipe_readable) {
    Reactor* r = reactor_new();
    ASSERT(r != NULL, "reactor_new should succeed");

    int pipefd[2];
    ASSERT(pipe(pipefd) == 0, "pipe should succeed");

    int sentinel = 0xBEEF;
    ASSERT(reactor_add(r, pipefd[0], true, false, &sentinel) == 0,
           "reactor_add should succeed");

    /* Write to pipe so read end becomes readable */
    const char* msg = "hello";
    write(pipefd[1], msg, strlen(msg));

    ReactorEvent events[4];
    int n = reactor_poll(r, events, 4, 100);
    ASSERT(n == 1, "poll should return 1 event");
    ASSERT(events[0].fd == pipefd[0], "event fd should be read end of pipe");
    ASSERT(events[0].readable == true, "event should be readable");
    ASSERT(events[0].userdata == &sentinel, "userdata should match");

    /* Drain the pipe */
    char buf[64];
    read(pipefd[0], buf, sizeof(buf));

    reactor_remove(r, pipefd[0]);
    close(pipefd[0]);
    close(pipefd[1]);
    reactor_free(r);
    return NULL;
}

/* Test: remove fd, poll should no longer fire */
TEST(reactor_remove_fd) {
    Reactor* r = reactor_new();
    int pipefd[2];
    pipe(pipefd);

    reactor_add(r, pipefd[0], true, false, NULL);
    reactor_remove(r, pipefd[0]);

    /* Write data */
    write(pipefd[1], "x", 1);

    ReactorEvent events[4];
    int n = reactor_poll(r, events, 4, 0);
    ASSERT(n == 0, "after remove, poll should return 0");

    close(pipefd[0]);
    close(pipefd[1]);
    reactor_free(r);
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(reactor_create_free);
    RUN_TEST(reactor_poll_timeout);
    RUN_TEST(reactor_pipe_readable);
    RUN_TEST(reactor_remove_fd);
    return NULL;
}

int main(void) {
    printf("Testing reactor...\n");
    RUN_SUITE(all_tests);
    return 0;
}
