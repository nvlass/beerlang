# Embedding beerlang

How to build the beerlang runtime into another program — a C application,
the browser (WASM), or an operating system written around it (beeros) —
and which hooks to use instead of patching the runtime.

The rule of thumb: **configure, don't patch.** Pick sources and features
with `beerlang.mk`, change behaviour through the runtime's hooks, and if a
hook is missing, ask for one upstream (beerlang's `TODO.md`). Don't keep
modified copies of beerlang files, mirror its private structs, or stub its
internal functions: those break silently when beerlang changes.

## 1. Building: `beerlang.mk`

`beerlang.mk` (repository root) lists the runtime's sources and flags. The
host `Makefile`, `wasm/Makefile.wasm` and embedders all include it:

```make
BEER_ROOT     := path/to/beerlang
BEER_PLATFORM := none          # switches: see below
BEER_CRYPTO   := 1
include $(BEER_ROOT)/beerlang.mk

SRCS   += $(BEER_SRCS) $(BEER_VENDOR_SRCS)
CFLAGS += $(BEER_CFLAGS)       # include paths + feature defines
```

| Switch | Values | Default | What it selects |
|--------|--------|---------|-----------------|
| `BEER_PLATFORM` | `posix`, `none` | `posix` | `posix`: kqueue/epoll I/O reactor (`src/io/reactor.c`, `io_reactor.c`). `none`: a stub reactor (`src/platform/none/io_reactor_none.c`) for targets without fd readiness — WASM, bare metal |
| `BEER_NET` | `1`/`0` | `1` on posix, `0` on none | `beer.tcp`, `beer.udp` natives (needs posix) |
| `BEER_SHELL` | `1`/`0` | `1` on posix, `0` on none | `beer.shell/exec` (needs posix) |
| `BEER_CRYPTO` | `1`/`0` | `1` | `beer.crypto` (sha256, hmac, random-bytes) |
| `BEER_AOT` | `1`/`0` | `1` | `.beerc` compile/load (`beer.beerc`) |
| `BEER_CFFI` | `1`/`0` | `0` | `beer.ffi` via libffi (link libffi yourself) |

A switched-off feature adds `-DBEER_NO_<FEATURE>` to `BEER_CFLAGS`, and
`namespace_init` doesn't register its natives — no stubs needed. With no
define at all every feature is on, so an older embedder Makefile keeps
building. Every other source file is core and is picked up automatically,
so a new runtime file needs no change in any build.

`BEER_SRCS` has no `main()`; the CLI's is `src/repl/main.c`. The `none`
platform calls `usleep` to wait for timers: libc and Emscripten have it, a
bare-metal embedder provides it (as beeros does).

The host build accepts the same switches: `make BEER_NET=0`,
`make BEER_PLATFORM=none` (a REPL without sockets or subprocesses).

## 2. Starting the runtime

```c
memory_init();
symbol_init();
namespace_init();        /* registers natives, creates the scheduler,
                            loads lib/core.beer from BEERPATH / lib/ */
```

Without a file system, load `core.beer` yourself (e.g. embedded with
`xxd -i`) and skip the search:

```c
namespace_init_bare();   /* everything but the lib/core.beer search */
VM* vm = vm_new(16);     /* only collects errors */
core_load_source_ns(vm, core_beer_src, "lib/core.beer", "beer.core");
if (vm->error) report(vm->error_msg);
vm_free(vm);
```

## 3. Loading and running code

- `core_load_source(vm, src, name)` reads, compiles and runs every form of
  `src` in the current namespace, then restores the current namespace (a
  file's `(ns ...)` doesn't stick). Each form runs as a task; errors are
  thrown in `vm` (fatal when it has no handler) and name the source and the
  form. It sets each function's home namespace and keeps the compiled code
  alive — don't free compiled units yourself.
- `core_load_source_ns(vm, src, name, ns)` does the same inside `ns`.
- From beerlang: `load-string`, `load-file`, `eval`, `require` all run in
  the calling task (they yield like any code; two REPLs don't stall each
  other).
- `include/beer.h` is a higher-level API for applications (`beer_open`,
  `beer_do_string`, `beer_call`, `beer_register`, ...); `make libbeerlang`
  builds it as a static library.

## 4. Extension points

**Natives.** Register C functions in a namespace:

```c
Value fn = native_function_new(-1, my_native, "my-native");  /* -1: any arity */
namespace_define(namespace_registry_get_or_create(global_namespace_registry, "my.ns"),
                 symbol_intern("my-native"), fn);
object_release(fn);
```

A native gets `(VM* vm, int argc, Value* argv)` and returns an owned
value. Signal errors with `vm_throw_error(vm, msg)` (catchable). A native
that needs to call a beerlang function asks the VM to make the call
instead of running it itself: `vm_request_call(vm, fn, argc, args)`, then
return `VALUE_NIL` (that is how `apply` works). Don't run beerlang code in
a VM of your own from a native: it can't yield, so it stalls every task.

**File system: `beer.core/*fs*`.** `slurp`, `spit`, `load-file`/`load` and
`require`'s `*load-path*` search go through it when it is set:

```clojure
(def beer.core/*fs*
  {:read   (fn [path] ...)            ; contents as a string, or nil
   :write! (fn [path s append?] ...)})
```

Not covered (still libc): `open` and streams on files, `beer.tar`, the
`BEERPATH` tar scan at startup.

**Interrupts.** `vm_request_interrupt()` stops the foreground task (the
REPL's form) and `vm_request_interrupt_task(task)` a given one; both only
set flags, so they are safe from a signal or IRQ handler. The scheduler
delivers them between quanta: the task unwinds with
`{:type :interrupt}`, skipping `catch` and running `finally`. From
beerlang: `(request-interrupt!)`, `(interrupt! task)`, `(current-task)`.
A REPL loop written in beerlang survives an interrupt of its own task with
`(try (eval form) (catch :interrupt e ...) (catch e ...))`. With no
foreground task a request stays pending: clear `vm_interrupt_requested` at
your prompt. Details: `docs/design/08_MULTITASKING.md`.

**Sessions (several REPLs).** Run each REPL in its own task inside a
`binding` of the session's dynamic vars:

```clojure
(binding [*ns* 'user  *out* term-stream  *in* kbd-stream]
  (repl-loop))
```

`in-ns` then changes only that session's namespace, and `println` goes to
its terminal; tasks it spawns inherit the bindings.

**Scheduler.** The scheduler is cooperative; the embedder drives it:

- `scheduler_run_task_to_completion(global_scheduler, task)` — run a task
  you made (`task_new_from_code`) to the end, running other ready tasks too.
- `scheduler_run_ready(global_scheduler)` — run what is ready and return
  (once per frame from a game loop).
- `scheduler_run_one_tick(global_scheduler)` — one quantum (polling loops,
  `irq-wait!`-style natives).

Producers that must never block (interrupt handlers, input backends) use
`(offer! ch v)` — puts only if a taker is waiting or the buffer has room —
and consumers that poll use `(poll! ch)`.

**Binary data and DMA.** `beer.bytes` (`TYPE_BYTEBUFFER`) is the buffer
type for drivers: `bytes/addr` gives the address of its data for DMA;
`bytes/blit-from-addr!` / `bytes/blit-to-addr!` copy from/to raw memory.
Use `bytebuffer_data()` / `string_cstr()` in C — never mirror the private
object layouts.

## 5. Moving beeros onto this

beeros builds beerlang from an unmodified submodule. What it can drop:

- [ ] Replace `BEER_EXCLUDE` + the wildcard source list with
      `BEER_PLATFORM := none` and `include beerlang/beerlang.mk`; compile
      `$(BEER_SRCS) $(BEER_VENDOR_SRCS)` with `$(BEER_CFLAGS)`.
- [ ] Delete `kernel/io_reactor_baremetal.c` (now
      `src/platform/none/io_reactor_none.c`) and
      `kernel/reactor_baremetal.c` (nothing outside `io_reactor.c` used the
      low-level reactor).
- [ ] Delete the `core_register_tcp/udp/shell` stubs in
      `kernel/syscall_stubs.c` (`BEER_NET`/`BEER_SHELL` are off on the
      `none` platform). Keep the libc-level stubs (`usleep`, `poll`, ...):
      those are the platform's.
- [ ] Replace `kernel/blob_loader.c` with `core_load_source_ns` (it sets
      function home namespaces, restores the namespace and `*ns*`, keeps
      compiled units alive).
- [ ] Keep `beer.mem`, `beer.gfx`, the drivers, console/term, fs/ext2/ustar
      and packages in beeros: they are platform, not language.
