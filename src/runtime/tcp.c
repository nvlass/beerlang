/* beer.tcp namespace — TCP socket natives */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
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
        if ((errno == EAGAIN || errno == EWOULDBLOCK) &&
            vm->scheduler && vm->scheduler->current) {
            Task* task = vm->scheduler->current;
            if (lst->blocked_task && lst->blocked_task != task) {
                vm_error(vm, "tcp/accept: stream already in use by another task");
                return VALUE_NIL;
            }
            lst->blocked_task = task;
            io_reactor_register(vm->scheduler->io_reactor,
                                lst->fd, true, false, task);
            scheduler_block_io(vm->scheduler, task);
            vm->native_blocked = true;
            vm->yielded = true;
            return VALUE_NIL;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "tcp/accept: accept() failed: %s", strerror(errno));
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    /* Clear blocked_task on success (retry path) */
    lst->blocked_task = NULL;

    /* Set client fd non-blocking */
    int fl = fcntl(client_fd, F_GETFL);
    if (fl >= 0) fcntl(client_fd, F_SETFL, fl | O_NONBLOCK);

    Value v = stream_from_fd(client_fd, true, true, true, STREAM_SOCKET);
    Stream* cs = (Stream*)untag_pointer(v);
    cs->nonblocking = true;
    return v;
}

/* (tcp-connect host port) — connect to remote host, returns stream */
static Value native_tcp_connect(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "tcp/connect: requires 2 arguments (host port)");
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

    /* Set non-blocking before connect */
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    /* Connect with blocking fallback — connect() is fast for localhost
     * and the native_blocked rewind pattern doesn't work well here since
     * we can't re-enter without creating a duplicate socket. */
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);  /* temporarily blocking */
    int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "tcp/connect: connect() failed: %s", strerror(errno));
        close(fd);
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);  /* restore non-blocking */

    Value v = stream_from_fd(fd, true, true, true, STREAM_SOCKET);
    Stream* s = (Stream*)untag_pointer(v);
    s->nonblocking = true;
    return v;
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
