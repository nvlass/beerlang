/* Tests for the platform reactor (kqueue/epoll) */

#include "test.h"
#include "reactor.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>

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

/* Test: watch pipe, write to it, poll detects readable */
TEST(reactor_pipe_readable) {
    Reactor* r = reactor_new();
    ASSERT(r != NULL, "reactor_new should succeed");

    int pipefd[2];
    ASSERT(pipe(pipefd) == 0, "pipe should succeed");

    ASSERT(reactor_set(r, pipefd[0], true, false) == 0, "reactor_set should succeed");

    /* Write to pipe so read end becomes readable */
    const char* msg = "hello";
    write(pipefd[1], msg, strlen(msg));

    ReactorEvent events[4];
    int n = reactor_poll(r, events, 4, 100);
    ASSERT(n == 1, "poll should return 1 event");
    ASSERT(events[0].fd == pipefd[0], "event fd should be read end of pipe");
    ASSERT(events[0].readable == true, "event should be readable");
    ASSERT(events[0].writable == false, "event should not be writable");

    /* Drain the pipe */
    char buf[64];
    read(pipefd[0], buf, sizeof(buf));

    reactor_set(r, pipefd[0], false, false);
    close(pipefd[0]);
    close(pipefd[1]);
    reactor_free(r);
    return NULL;
}

/* Test: clearing interest stops events */
TEST(reactor_clear_interest) {
    Reactor* r = reactor_new();
    int pipefd[2];
    pipe(pipefd);

    reactor_set(r, pipefd[0], true, false);
    reactor_set(r, pipefd[0], false, false);

    /* Write data */
    write(pipefd[1], "x", 1);

    ReactorEvent events[4];
    int n = reactor_poll(r, events, 4, 0);
    ASSERT(n == 0, "after clearing interest, poll should return 0");

    close(pipefd[0]);
    close(pipefd[1]);
    reactor_free(r);
    return NULL;
}

/* Test: read and write interest are independent; dropping one keeps the other */
TEST(reactor_directions_independent) {
    Reactor* r = reactor_new();
    int sv[2];
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair should succeed");

    reactor_set(r, sv[0], true, true);
    ReactorEvent events[4];
    int n = reactor_poll(r, events, 4, 100);
    ASSERT(n == 1 && events[0].writable && !events[0].readable,
           "idle socket should be writable only");

    reactor_set(r, sv[0], true, false);
    n = reactor_poll(r, events, 4, 0);
    ASSERT(n == 0, "with write interest dropped, an idle socket has no events");

    write(sv[1], "x", 1);
    n = reactor_poll(r, events, 4, 100);
    ASSERT(n == 1 && events[0].readable, "read interest survives dropping write");

    reactor_set(r, sv[0], false, false);
    close(sv[0]);
    close(sv[1]);
    reactor_free(r);
    return NULL;
}

/* Test suite */
static const char* all_tests(void) {
    RUN_TEST(reactor_create_free);
    RUN_TEST(reactor_poll_timeout);
    RUN_TEST(reactor_pipe_readable);
    RUN_TEST(reactor_clear_interest);
    RUN_TEST(reactor_directions_independent);
    return NULL;
}

int main(void) {
    printf("Testing reactor...\n");
    RUN_SUITE(all_tests);
    return 0;
}
