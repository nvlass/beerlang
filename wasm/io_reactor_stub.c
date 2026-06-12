/* WASM stub: no async I/O reactor needed in the browser.
 * All blocking I/O natives (tcp, udp, shell) are excluded from the WASM
 * build, so this stub only needs to satisfy the linker. */

#include "io_reactor.h"
#include <stdlib.h>

struct IOReactor { int dummy; };

IOReactor* io_reactor_new(void)                                           { return (IOReactor*)calloc(1, sizeof(IOReactor)); }
void       io_reactor_free(IOReactor* r)                                  { free(r); }
void       io_reactor_register(IOReactor* r, int fd, bool read, bool write, Task* task) { (void)r;(void)fd;(void)read;(void)write;(void)task; }
void       io_reactor_unregister(IOReactor* r, int fd)                    { (void)r;(void)fd; }
int        io_reactor_drain(IOReactor* r, Task** out, int max)            { (void)r;(void)out;(void)max; return 0; }
