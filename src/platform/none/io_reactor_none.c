/* I/O reactor for platforms without fd readiness (BEER_PLATFORM=none):
 * WASM in the browser, bare metal (beeros). With BEER_NET and BEER_SHELL
 * off no native ever parks a task on an fd, so the waiting functions only
 * satisfy the scheduler; poll just honours the scheduler's timeout so
 * sleeping tasks don't busy-spin (usleep comes from the platform: libc,
 * Emscripten, or the embedder's own). Replaces io_reactor.c and reactor.c. */

#include "io_reactor.h"
#include <unistd.h>

struct IOReactor { int dummy; };

static struct IOReactor reactor;

IOReactor* io_reactor_new(void)                                       { return &reactor; }
void       io_reactor_free(IOReactor* r)                              { (void)r; }
void       io_reactor_wait(IOReactor* r, int fd, bool write, Task* t) { (void)r; (void)fd; (void)write; (void)t; }
void       io_reactor_cancel_fd(IOReactor* r, Scheduler* s, int fd)   { (void)r; (void)s; (void)fd; }
void       io_reactor_forget(IOReactor* r, Task* t)                   { (void)r; (void)t; }
int        io_reactor_waiting(IOReactor* r)                           { (void)r; return 0; }

int io_reactor_poll(IOReactor* r, Scheduler* s, int timeout_ms, int watch_fd, bool* watch_ready) {
    (void)r; (void)s;
    /* No readiness to wait for: report a watched fd (the CLI REPL's stdin)
     * ready, and let the caller's read block instead */
    if (watch_fd >= 0) {
        if (watch_ready) *watch_ready = true;
        return 0;
    }
    if (watch_ready) *watch_ready = false;
    /* Never block indefinitely: nothing here could wake us */
    if (timeout_ms > 0) usleep((useconds_t)timeout_ms * 1000);
    return 0;
}
