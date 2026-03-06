# Porting Beerlang to FreeRTOS

Assessment of the required effort, changes, and trade-offs for running the beerlang VM on FreeRTOS-based embedded systems.

---

## 1. Executive Summary

Beerlang's C11 codebase is **well-suited for an embedded port**. The cooperative multitasking model maps naturally to FreeRTOS, there are no hard POSIX dependencies (no pthreads, signals, fork, mmap, dlopen), and the only external library (mini-GMP) was designed for embedded use. The main challenges are dynamic memory allocation pressure, Unix fd-based I/O, and a few POSIX-specific libc calls.

**Estimated total effort:** 12-16 weeks for a full-featured port, 4-6 weeks for a minimal VM-only port.

---

## 2. Platform Dependencies Audit

### 2.1 Clean — No Issues

| Dependency | Status |
|---|---|
| pthreads | Not used (cooperative green threads only) |
| signals | Not used |
| fork / exec | Not used |
| mmap | Not used |
| dlopen / dlsym | Not used |
| getenv | Not used |
| epoll / kqueue | Not yet implemented (planned for I/O reactor) |

### 2.2 Needs Replacement

| Item | Location | Issue | Effort |
|---|---|---|---|
| `open_memstream` | `src/vm/value.c:301` | POSIX-only, used by `value_sprint_readable` to capture printf output into a buffer | Low — rewrite with `snprintf` into a fixed/growing buffer |
| fd-based I/O | `src/io/stream.c` | `open()`, `close()`, `read()`, `write()` via `<fcntl.h>` / `<unistd.h>` | Medium — abstract behind a platform I/O layer |
| `fopen` / `fgets` / `fread` | `src/reader/buffer.c`, `src/runtime/core.c` | File-based source loading (`load`, `slurp`, `spit`) | Medium — abstract or remove |
| `printf` / `fprintf` family | ~344 occurrences across all files | Debug output, error messages, value printing | Medium — wrap in macros, replace with lightweight printf or UART writes |
| `<time.h>` | `lib/ulog.c` | Timestamps in logging | Low — disable or stub |

### 2.3 Standard C — Should Work

These are available in typical embedded C libraries (newlib, picolibc):

- `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<limits.h>` — type definitions
- `<string.h>` — `memcpy`, `memset`, `strcmp`, `strlen`, etc.
- `<ctype.h>` — character classification (reader)
- `<assert.h>` — invariant checks (can map to `configASSERT`)
- `malloc` / `calloc` / `realloc` / `free` — map to `pvPortMalloc` / `vPortFree`

---

## 3. Memory Analysis

### 3.1 Per-VM Footprint

| Structure | Size | Notes |
|---|---|---|
| VM value stack | 256 slots x 16 bytes = **4 KB** | `DEFAULT_STACK_SIZE`, tunable |
| Call frames | 64 frames x ~40 bytes = **2.5 KB** | |
| Exception handler stack | 16 x 12 bytes = **192 B** | |
| Stream buffers (stdin/stdout/stderr) | 3 x 2 x 8 KB = **48 KB** | `STREAM_BUF_SIZE` — can reduce |
| **Per-VM total** | **~55 KB** | Before any user allocations |

Each spawned **Task** owns its own VM, so multiply by concurrent task count.

### 3.2 Global / One-Time Costs

| Structure | Size | Notes |
|---|---|---|
| Symbol intern table | ~4-10 KB | Grows with unique symbols |
| Namespace registry | ~2-4 KB | `beer.core` + user ns |
| Native function table | ~50 entries x 8 B = ~400 B | |
| Compiled code pool (REPL) | Up to 1024 units | `MAX_COMPILED_UNITS`, reducible |
| Reader input buffer | 4 KB | `INITIAL_BUFFER_SIZE` |
| REPL input buffer | 4 KB | `INPUT_BUFFER_SIZE` |

### 3.3 Code Size Estimate (ARM Thumb-2, -Os)

| Module | Estimated Size |
|---|---|
| VM core (`vm.c`, `value.c`, `fixnum.c`) | 30-40 KB |
| Types (string, cons, vector, hashmap, symbol, bigint) | 20-30 KB |
| Compiler | 25-35 KB |
| Reader | 10-15 KB |
| Runtime / natives | 20-25 KB |
| mini-GMP | 20-30 KB |
| Scheduler + task + channel | 5-8 KB |
| I/O / streams | 5-8 KB |
| **Total** | **~135-190 KB** |

Can reduce by 20-30 KB by removing bigint support. Can reduce further with LTO.

### 3.4 Minimum Viable Platform

| Resource | Minimum | Comfortable |
|---|---|---|
| Flash | 256 KB (no stdlib .beer) | 512 KB+ |
| RAM | 96 KB (1 task, reduced buffers) | 256 KB+ |
| Architecture | 32-bit with 64-bit int support | ARM Cortex-M4+ |

### 3.5 Target Platform Fit

| Platform | Flash | RAM | Fit |
|---|---|---|---|
| ESP32 / ESP32-S3 | 4 MB+ | 320-520 KB | Excellent |
| STM32F7 (Cortex-M7) | 1-2 MB | 512 KB-1 MB | Excellent |
| IMXRT1062 (Teensy 4.x) | 2 MB | 1 MB | Excellent |
| STM32F4 (Cortex-M4) | 512 KB-1 MB | 128-192 KB | Tight but workable |
| RP2040 (Cortex-M0+) | 2 MB ext flash | 264 KB | Possible, limited tasks |
| STM32F1/L0 (Cortex-M0/3, <64 KB RAM) | — | — | Not viable |

---

## 4. What Must Change

### 4.1 Remove or Disable

| Feature | Reason | Impact |
|---|---|---|
| File-based `load` / `require` | No filesystem on many MCUs | Remove file loading; load from flash or compile-in |
| `slurp` / `spit` | Requires filesystem | Remove or gate behind `BEER_ENABLE_FILE_IO` |
| `*load-path*` / `*loaded-libs*` | File-path-based module resolution | Replace with flash-based lookup or remove |
| REPL line editing | Assumes terminal (`isatty`, line buffering) | Replace with bare UART read |
| ulog timestamps | `<time.h>` | Stub or use FreeRTOS tick count |

### 4.2 Replace / Abstract

| Component | Current | FreeRTOS Replacement |
|---|---|---|
| `malloc` / `free` | libc heap | `pvPortMalloc` / `vPortFree` (heap_4 or heap_5) |
| `aligned_alloc` (alloc.c) | libc | Custom: `pvPortMalloc` + manual alignment padding |
| `open_memstream` (value.c) | POSIX | `snprintf` into stack/heap buffer |
| `printf` / `fprintf` | libc stdio | Lightweight printf (tinyprintf) or UART write wrapper |
| Stream I/O (stream.c) | Unix fd `open/read/write/close` | Platform abstraction: UART for console, FatFS/LittleFS for files |
| `assert()` | libc | `configASSERT()` |
| mini-GMP allocator | libc malloc | `mp_set_memory_functions(pvPortMalloc, beer_realloc, vPortFree)` |

Note: FreeRTOS `pvPortMalloc` does not provide `realloc`. A wrapper is needed:

```c
void* beer_realloc(void* ptr, size_t old_size, size_t new_size) {
    void* new_ptr = pvPortMalloc(new_size);
    if (new_ptr && ptr) {
        memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
        vPortFree(ptr);
    }
    return new_ptr;
}
```

### 4.3 Modify

| Component | Change |
|---|---|
| `src/io/stream.c` | Rewrite to use abstract I/O backend (UART / VFS) instead of Unix fds |
| `src/vm/value.c` | Replace `open_memstream` with buffer-based approach |
| `src/repl/main.c` | UART-based input instead of `fgets(stdin)` |
| `src/runtime/namespace.c` | Remove or conditionalize file-loading code paths |
| `lib/core.beer` | Either compile-in as a C byte array or load from flash |
| Buffer sizes | Reduce `STREAM_BUF_SIZE` (8 KB -> 1-2 KB), `DEFAULT_STACK_SIZE` (256 -> 128) |

---

## 5. What Can Stay Unchanged

The following are portable and need no changes:

- **VM execution loop** (`src/vm/vm.c`) — pure computation, no OS calls
- **All type implementations** — strings, cons, vectors, hashmaps, symbols
- **Compiler** (`src/compiler/compiler.c`) — pure transformation
- **Reader** (`src/reader/reader.c`) — operates on in-memory buffers
- **Reference counting** (`src/memory/alloc.c`) — just pointer arithmetic
- **Scheduler** (`src/scheduler/scheduler.c`) — cooperative, no OS primitives
- **Tasks** (`src/task/task.c`) — green threads, no OS threads
- **Channels** (`src/channel/channel.c`) — pure data structures + scheduler calls
- **mini-GMP** (`lib/mini-gmp.c`) — designed for embedded
- **Core natives** (`src/runtime/core.c`) — arithmetic, collections, predicates (except I/O natives)
- **Core macros** (`lib/core.beer`) — pure beerlang, fully portable

This is roughly **80-85% of the codebase** unchanged.

---

## 6. Risks

### High

- **Heap fragmentation.** Long-running embedded systems with frequent alloc/free of variable-size objects will fragment the heap. Mitigation: use heap_4 (coalescing free), add object pools for common sizes, or periodic restart.
- **Stack overflow.** The reader and compiler use C recursion proportional to source nesting depth. On FreeRTOS tasks with 2-4 KB C stack, deeply nested code will crash. Mitigation: limit nesting depth or convert to iterative with explicit stack.

### Medium

- **RAM pressure.** Each concurrent beerlang task adds ~6-8 KB (VM + stack). Running 10+ tasks on a 128 KB MCU is tight. Mitigation: limit task count, reduce stack sizes.
- **No `realloc`.** FreeRTOS standard heaps don't support `realloc`. Several components use it (vectors, reader buffer, bytecode buffer). Mitigation: alloc-copy-free wrapper (shown above).

### Low

- **Performance.** Bytecode interpretation on Cortex-M4 at 168 MHz should achieve ~500K-2M VM instructions/sec — adequate for scripting and control logic, not for compute-heavy workloads.
- **Cooperative scheduling fit.** The existing model is already cooperative, which is the norm on FreeRTOS. No conceptual mismatch.

---

## 7. Recommended Approach

### Phase 1 — Minimal VM on FreeRTOS (4-6 weeks)

Create a platform abstraction layer and get the VM executing pre-compiled bytecode.

1. Add `include/platform.h` with macros for malloc/free/printf/assert
2. Replace `open_memstream` in `value.c`
3. Stub out all file I/O (return errors)
4. Replace printf with UART output
5. Cross-compile for target MCU
6. Run hand-crafted bytecode tests

**Deliverable:** VM runs arithmetic, function calls, closures on MCU.

### Phase 2 — REPL over UART (4-6 weeks)

Bring up the reader and compiler for interactive use.

1. Implement UART-based stream for stdin/stdout
2. Compile `lib/core.beer` into a C byte array (embed in flash)
3. Load core.beer at startup from the embedded array
4. REPL loop reading from UART
5. Test interactively over serial console

**Deliverable:** Full interactive REPL on embedded target.

### Phase 3 — Full Feature Port (4-6 weeks)

Production readiness.

1. Optional VFS layer for file I/O (FatFS on SD card)
2. Tune buffer sizes and stack depths for target
3. Add watchdog-friendly yield points
4. Memory usage profiling and optimization
5. Stress testing (long-running, many tasks)

**Deliverable:** Production-grade beerlang runtime on FreeRTOS.

---

## 8. Conclusion

Beerlang is unusually portable for a VM-based language. ~80% of the code is pure C11 computation with no OS dependencies. The cooperative multitasking model is a natural fit for FreeRTOS. The main work is:

1. **I/O abstraction** (~40% of porting effort) — replacing Unix fd-based streams
2. **Memory adaptation** (~30%) — heap wrappers, `realloc` shim, fragmentation strategy
3. **Build integration** (~20%) — cross-compilation, flash embedding of core.beer
4. **Miscellaneous** (~10%) — `open_memstream`, printf, assert

A minimal port (VM + REPL over UART) is achievable in **8-12 weeks** by a single developer familiar with both the codebase and FreeRTOS.
