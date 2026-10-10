/* WASM stub: no async I/O reactor needed in the browser.
 * All blocking I/O natives (tcp, udp, shell) are excluded from the WASM
 * build, so no task ever waits on an fd; poll only has to honour the
 * scheduler's timeout so sleeping tasks don't busy-spin. */

#include "io_reactor.h"
#include <stdlib.h>
#include <unistd.h>

struct IOReactor { int dummy; };

IOReactor* io_reactor_new(void)                                       { return (IOReactor*)calloc(1, sizeof(IOReactor)); }
void       io_reactor_free(IOReactor* r)                              { free(r); }
void       io_reactor_wait(IOReactor* r, int fd, bool write, Task* t) { (void)r;(void)fd;(void)write;(void)t; }
void       io_reactor_cancel_fd(IOReactor* r, Scheduler* s, int fd)   { (void)r;(void)s;(void)fd; }
void       io_reactor_forget(IOReactor* r, Task* t)                   { (void)r;(void)t; }
int        io_reactor_waiting(IOReactor* r)                           { (void)r; return 0; }

int io_reactor_poll(IOReactor* r, Scheduler* s, int timeout_ms, int watch_fd, bool* watch_ready) {
    (void)r; (void)s; (void)watch_fd;
    if (watch_ready) *watch_ready = false;
    if (timeout_ms > 0) usleep((useconds_t)timeout_ms * 1000);
    return 0;
}
