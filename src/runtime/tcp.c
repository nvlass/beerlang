/* beer.tcp namespace — TCP socket natives */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <poll.h>
#include <time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "vm.h"
#include "value.h"
#include "bstring.h"
#include "stream.h"
#include "symbol.h"
#include "namespace.h"
#include "memory.h"
#include "native.h"
#include "scheduler.h"
#include "io_reactor.h"
#include "task.h"
#include "core.h"

/* Forward declaration — defined in core.c */
extern NamespaceRegistry* global_namespace_registry;

/* (tcp-listen port) or (tcp-listen port backlog) — create listening socket */
static Value native_tcp_listen(VM* vm, int argc, Value* argv) {
    if (argc < 1 || argc > 2) {
        vm_error(vm, "tcp/listen: requires 1-2 arguments (port [backlog])");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[0])) {
        vm_error(vm, "tcp/listen: port must be an integer");
        return VALUE_NIL;
    }
    int port = (int)untag_fixnum(argv[0]);
    int backlog = 128;
    if (argc == 2) {
        if (!is_fixnum(argv[1])) {
            vm_error(vm, "tcp/listen: backlog must be an integer");
            return VALUE_NIL;
        }
        backlog = (int)untag_fixnum(argv[1]);
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "tcp/listen: socket() failed: %s", strerror(errno));
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "tcp/listen: bind() failed on port %d: %s", port, strerror(errno));
        close(fd);
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    if (listen(fd, backlog) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "tcp/listen: listen() failed: %s", strerror(errno));
        close(fd);
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    /* Set non-blocking */
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    Value v = stream_from_fd(fd, true, false, true, STREAM_SOCKET);
    Stream* s = (Stream*)untag_pointer(v);
    s->nonblocking = true;
    return v;
}

/* (tcp-accept listen-stream) — accept a connection, returns client stream */
static Value native_tcp_accept(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "tcp/accept: requires 1 argument (listen-stream)");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "tcp/accept: argument must be a stream");
        return VALUE_NIL;
    }

    Stream* lst = (Stream*)untag_pointer(argv[0]);

    /* If stream was closed (e.g. by stop!) return nil gracefully */
    if (lst->closed) return VALUE_NIL;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(lst->fd, (struct sockaddr*)&client_addr, &addr_len);

    if (client_fd < 0) {
        if ((errno == EAGAIN || errno == EWOULDBLOCK) && scheduler_can_park(vm)) {
            scheduler_park_native(vm, lst->fd, false);
            return VALUE_NIL;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "tcp/accept: accept() failed: %s", strerror(errno));
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    /* Set client fd non-blocking */
    int fl = fcntl(client_fd, F_GETFL);
    if (fl >= 0) fcntl(client_fd, F_SETFL, fl | O_NONBLOCK);

    Value v = stream_from_fd(client_fd, true, true, true, STREAM_SOCKET);
    Stream* cs = (Stream*)untag_pointer(v);
    cs->nonblocking = true;
    return v;
}

/* (tcp-connect host port) or (tcp-connect host port timeout-ms) — connect to remote host.
 * timeout-ms defaults to 10000 (10 s). Throws a catchable error on failure or timeout. */
static int64_t monotonic_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

/* Park until fd is writable (connect finished) or the connect deadline */
static void park_connect(VM* vm, int fd) {
    scheduler_park_native(vm, fd, true);
    scheduler_add_timer(vm->scheduler, vm->scheduler->current, vm->pending_deadline);
}

/* fd's connect has finished: check its outcome and wrap it in a stream */
static Value connect_result(VM* vm, int fd) {
    int sockerr = 0;
    socklen_t len = sizeof(sockerr);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &len);
    if (sockerr != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "tcp/connect: connect() failed: %s", strerror(sockerr));
        close(fd);
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }
    /* Socket is connected; keep non-blocking */
    Value v = stream_from_fd(fd, true, true, true, STREAM_SOCKET);
    Stream* s = (Stream*)untag_pointer(v);
    s->nonblocking = true;
    return v;
}

static Value native_tcp_connect(VM* vm, int argc, Value* argv) {
    if (argc < 2 || argc > 3) {
        vm_error(vm, "tcp/connect: requires 2 or 3 arguments (host port [timeout-ms])");
        return VALUE_NIL;
    }
    if (!is_string(argv[0])) {
        vm_error(vm, "tcp/connect: host must be a string");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[1])) {
        vm_error(vm, "tcp/connect: port must be an integer");
        return VALUE_NIL;
    }
    int timeout_ms = 10000;
    if (argc == 3) {
        if (!is_fixnum(argv[2])) {
            vm_error(vm, "tcp/connect: timeout-ms must be an integer");
            return VALUE_NIL;
        }
        timeout_ms = (int)untag_fixnum(argv[2]);
        if (timeout_ms <= 0) {
            vm_error(vm, "tcp/connect: timeout-ms must be positive");
            return VALUE_NIL;
        }
    }

    /* Retry after parking: the connect started earlier is in pending_fd */
    if (vm->pending_fd >= 0) {
        int fd = vm->pending_fd;
        Task* task = vm->scheduler->current;
        io_reactor_forget(vm->scheduler->io_reactor, task);
        scheduler_cancel_timer(vm->scheduler, task);
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        if (poll(&pfd, 1, 0) == 0) {
            if (monotonic_ns() < vm->pending_deadline) {
                park_connect(vm, fd);
                return VALUE_NIL;
            }
            vm->pending_fd = -1;
            close(fd);
            char buf[128];
            snprintf(buf, sizeof(buf), "tcp/connect: connection timed out after %d ms", timeout_ms);
            vm_throw_error(vm, buf);
            return VALUE_NIL;
        }
        vm->pending_fd = -1;
        return connect_result(vm, fd);
    }

    const char* host = string_cstr(argv[0]);
    int port = (int)untag_fixnum(argv[1]);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        /* Try DNS resolution */
        struct hostent* he = gethostbyname(host);
        if (!he) {
            char buf[256];
            snprintf(buf, sizeof(buf), "tcp/connect: cannot resolve host '%s'", host);
            vm_throw_error(vm, buf);
            return VALUE_NIL;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "tcp/connect: socket() failed: %s", strerror(errno));
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    /* Non-blocking connect + select() with user-specified timeout */
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) fl = 0;
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        char buf[256];
        snprintf(buf, sizeof(buf), "tcp/connect: connect() failed: %s", strerror(errno));
        close(fd);
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    if (rc != 0) {
        /* EINPROGRESS. In a task, park until writable or the deadline. */
        if (scheduler_can_park(vm)) {
            vm->pending_fd = fd;
            vm->pending_deadline = monotonic_ns() + (int64_t)timeout_ms * 1000000;
            park_connect(vm, fd);
            return VALUE_NIL;
        }
        /* No task to park: wait here, blocking the thread */
        fd_set wfds, efds;
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(fd, &wfds);
        FD_SET(fd, &efds);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int nready = select(fd + 1, NULL, &wfds, &efds, &tv);
        if (nready == 0) {
            close(fd);
            char buf[128];
            snprintf(buf, sizeof(buf), "tcp/connect: connection timed out after %d ms", timeout_ms);
            vm_throw_error(vm, buf);
            return VALUE_NIL;
        }
        if (nready < 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "tcp/connect: select() failed: %s", strerror(errno));
            close(fd);
            vm_throw_error(vm, buf);
            return VALUE_NIL;
        }
    }

    return connect_result(vm, fd);
}

/* (tcp-local-port stream) — get local port of a socket */
static Value native_tcp_local_port(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "tcp/local-port: requires 1 argument");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "tcp/local-port: argument must be a stream");
        return VALUE_NIL;
    }
    Stream* s = (Stream*)untag_pointer(argv[0]);
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(s->fd, (struct sockaddr*)&addr, &len) < 0) {
        vm_error(vm, "tcp/local-port: getsockname() failed");
        return VALUE_NIL;
    }
    return make_fixnum(ntohs(addr.sin_port));
}

static void register_native_in_ns(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
}

void core_register_tcp(void) {
    Namespace* tcp_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.tcp");
    if (!tcp_ns) return;
    register_native_in_ns(tcp_ns, "tcp-listen", native_tcp_listen);
    register_native_in_ns(tcp_ns, "tcp-accept", native_tcp_accept);
    register_native_in_ns(tcp_ns, "tcp-connect", native_tcp_connect);
    /* tcp-close not needed — beer.core/close works on any stream */
    register_native_in_ns(tcp_ns, "tcp-local-port", native_tcp_local_port);
}
