/* beer.udp namespace — UDP socket natives */

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
#include "hashmap.h"
#include "core.h"

extern NamespaceRegistry* global_namespace_registry;

/* (udp/socket) — create an unbound UDP socket */
static Value native_udp_socket(VM* vm, int argc, Value* argv) {
    (void)argv;
    if (argc != 0) {
        vm_error(vm, "udp/socket: takes no arguments");
        return VALUE_NIL;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "udp/socket: socket() failed: %s", strerror(errno));
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    Value v = stream_from_fd(fd, true, true, true, STREAM_SOCKET);
    Stream* s = (Stream*)untag_pointer(v);
    s->nonblocking = true;
    return v;
}

/* (udp/bind port) or (udp/bind host port) — create a UDP socket bound to a port */
static Value native_udp_bind(VM* vm, int argc, Value* argv) {
    if (argc < 1 || argc > 2) {
        vm_error(vm, "udp/bind: requires 1 or 2 arguments (port) or (host port)");
        return VALUE_NIL;
    }

    const char* host = NULL;
    int port;
    if (argc == 1) {
        if (!is_fixnum(argv[0])) {
            vm_error(vm, "udp/bind: port must be an integer");
            return VALUE_NIL;
        }
        port = (int)untag_fixnum(argv[0]);
    } else {
        if (!is_string(argv[0])) {
            vm_error(vm, "udp/bind: host must be a string");
            return VALUE_NIL;
        }
        if (!is_fixnum(argv[1])) {
            vm_error(vm, "udp/bind: port must be an integer");
            return VALUE_NIL;
        }
        host = string_cstr(argv[0]);
        port = (int)untag_fixnum(argv[1]);
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "udp/bind: socket() failed: %s", strerror(errno));
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (host) {
        if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
            struct hostent* he = gethostbyname(host);
            if (!he) {
                char buf[256];
                snprintf(buf, sizeof(buf), "udp/bind: cannot resolve host '%s'", host);
                close(fd);
                vm_throw_error(vm, buf);
                return VALUE_NIL;
            }
            memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
        }
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "udp/bind: bind() failed on port %d: %s", port, strerror(errno));
        close(fd);
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }

    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    Value v = stream_from_fd(fd, true, true, true, STREAM_SOCKET);
    Stream* s = (Stream*)untag_pointer(v);
    s->nonblocking = true;
    return v;
}

/* (udp/send sock host port data) — send a datagram.
 * data can be a string. Returns nil. */
static Value native_udp_send(VM* vm, int argc, Value* argv) {
    if (argc != 4) {
        vm_error(vm, "udp/send: requires 4 arguments (sock host port data)");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "udp/send: first argument must be a udp socket");
        return VALUE_NIL;
    }
    if (!is_string(argv[1])) {
        vm_error(vm, "udp/send: host must be a string");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[2])) {
        vm_error(vm, "udp/send: port must be an integer");
        return VALUE_NIL;
    }
    if (!is_string(argv[3])) {
        vm_error(vm, "udp/send: data must be a string");
        return VALUE_NIL;
    }

    Stream* s = (Stream*)untag_pointer(argv[0]);
    if (s->closed) {
        vm_error(vm, "udp/send: socket is closed");
        return VALUE_NIL;
    }

    const char* host = string_cstr(argv[1]);
    int port = (int)untag_fixnum(argv[2]);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        struct hostent* he = gethostbyname(host);
        if (!he) {
            char buf[256];
            snprintf(buf, sizeof(buf), "udp/send: cannot resolve host '%s'", host);
            vm_throw_error(vm, buf);
            return VALUE_NIL;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    size_t data_len = string_byte_length(argv[3]);
    const char* data_ptr = string_cstr(argv[3]);

    ssize_t n = sendto(s->fd, data_ptr, data_len, 0,
                       (struct sockaddr*)&addr, sizeof(addr));
    if (n < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "udp/send: sendto() failed: %s", strerror(errno));
        vm_throw_error(vm, buf);
        return VALUE_NIL;
    }
    return VALUE_NIL;
}

/* (udp/recv sock max-bytes) — receive a datagram.
 * Blocks cooperatively until a packet arrives.
 * Returns {:data "..." :host "x.x.x.x" :port N} or nil if socket closed. */
static Value native_udp_recv(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "udp/recv: requires 2 arguments (sock max-bytes)");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "udp/recv: first argument must be a udp socket");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[1])) {
        vm_error(vm, "udp/recv: max-bytes must be an integer");
        return VALUE_NIL;
    }

    Stream* s = (Stream*)untag_pointer(argv[0]);
    if (s->closed) return VALUE_NIL;

    int max_bytes = (int)untag_fixnum(argv[1]);
    if (max_bytes <= 0 || max_bytes > 65507) max_bytes = 65507;

    char* buf = malloc((size_t)max_bytes);
    if (!buf) {
        vm_error(vm, "udp/recv: out of memory");
        return VALUE_NIL;
    }

    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    ssize_t n = recvfrom(s->fd, buf, (size_t)max_bytes, 0,
                         (struct sockaddr*)&peer, &peer_len);

    if (n < 0) {
        if ((errno == EAGAIN || errno == EWOULDBLOCK) &&
            vm->scheduler && vm->scheduler->current) {
            free(buf);
            Task* task = vm->scheduler->current;
            if (s->blocked_task && s->blocked_task != task) {
                vm_error(vm, "udp/recv: socket already in use by another task");
                return VALUE_NIL;
            }
            s->blocked_task = task;
            io_reactor_register(vm->scheduler->io_reactor,
                                s->fd, true, false, task);
            scheduler_block_io(vm->scheduler, task);
            vm->native_blocked = true;
            vm->yielded = true;
            return VALUE_NIL;
        }
        free(buf);
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "udp/recv: recvfrom() failed: %s", strerror(errno));
        vm_throw_error(vm, errbuf);
        return VALUE_NIL;
    }

    /* Clear blocked_task on successful recv (retry path) */
    s->blocked_task = NULL;

    /* Build result map {:data "..." :host "x.x.x.x" :port N} */
    Value data_str = string_from_buffer(buf, (size_t)n);
    free(buf);

    char host_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer.sin_addr, host_str, sizeof(host_str));
    Value host_val = string_from_cstr(host_str);
    Value port_val = make_fixnum(ntohs(peer.sin_port));

    Value result = hashmap_create_default();
    hashmap_set(result, keyword_intern("data"), data_str);
    hashmap_set(result, keyword_intern("host"), host_val);
    hashmap_set(result, keyword_intern("port"), port_val);

    object_release(data_str);
    object_release(host_val);

    return result;
}

/* (udp/local-port sock) — get bound local port */
static Value native_udp_local_port(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "udp/local-port: requires 1 argument");
        return VALUE_NIL;
    }
    if (!is_stream(argv[0])) {
        vm_error(vm, "udp/local-port: argument must be a udp socket");
        return VALUE_NIL;
    }
    Stream* s = (Stream*)untag_pointer(argv[0]);
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(s->fd, (struct sockaddr*)&addr, &len) < 0) {
        vm_error(vm, "udp/local-port: getsockname() failed");
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

void core_register_udp(void) {
    Namespace* udp_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.udp");
    if (!udp_ns) return;
    register_native_in_ns(udp_ns, "udp-socket",     native_udp_socket);
    register_native_in_ns(udp_ns, "udp-bind",       native_udp_bind);
    register_native_in_ns(udp_ns, "udp-send",       native_udp_send);
    register_native_in_ns(udp_ns, "udp-recv",       native_udp_recv);
    register_native_in_ns(udp_ns, "udp-local-port", native_udp_local_port);
}
