# Beerlang Implementation TODO

> **Note:** Remember to update PROGRESS.md when completing major milestones!

## Implementation Status Summary

**All tests passing, 100% pass rate**
- Unit tests: 61
- REPL smoke tests: 445
- **Last Updated:** 2026-04-15

## Completed Phases

### Phase 1-6: Foundation through REPL v1 — COMPLETE
- Tagged pointer value representation (16-byte tagged union struct)
- Memory management with reference counting
- All basic types: fixnum (full int64), bigint, string, symbol, keyword, char, bool, nil
- Cons cells, vectors, hashmaps
- VM core (stack, arithmetic, control flow, closures, tail calls)
- S-expression reader (full Clojure syntax)
- Compiler (all special forms, closures, loop/recur)
- REPL with namespace/var system

### Phase 7: Macro System — COMPLETE
- Variadic functions (`&` rest params)
- Reader: quasiquote, unquote, unquote-splicing
- `defmacro`, macro expansion at compile time (temp VM pattern)
- `in-ns`, `load` natives
- `lib/core.beer` core macros

### Post-Phase 7 Additions — COMPLETE
- Vector/map literal compilation: `[1 2 3]` → `(vector ...)`, `{:a 1}` → `(hash-map ...)`
- Hashmap hashing (`value_hash` for TYPE_HASHMAP)
- Cross-type sequence equality: `(= '(1 2 3) [1 2 3])` → true
- `apply` with bytecode functions (temp VM pattern)
- Core utilities: `mod`, `rem`, `inc`, `dec`, `zero?`, `pos?`, `neg?`, `even?`, `odd?`, `not=`, `identity`, `constantly`, `complement`, `filter`, `reduce`, `map`
- `let` destructuring via macro: `[a b c]`, `[a & rest]`, `[a b :as all]`
  - Compiler primitive renamed to `let*`, `let` is a macro in `lib/core.beer`
- `symbol`, `gensym` native functions
- `macroexpand-1`, `macroexpand` native functions
- `cond`, `->`, `->>` macros
- Var and Namespace as proper heap objects (refcounted, with destructors)

### Exception Handling — COMPLETE
- `try`/`catch`/`finally` special forms
- `throw` special form (maps only)
- Stack unwinding with proper refcount cleanup (manual unwinding, mirrors RETURN)
- `ex-info` helper in core.beer
- 4 new VM opcodes: PUSH_HANDLER, POP_HANDLER, THROW, LOAD_EXCEPTION
- `finally` body emitted twice in bytecode (after try + after catch), no new opcodes

### Standard Library — COMPLETE
- `>=`, `<=` comparison operators
- `second`, `last`, `butlast`, `take`, `drop`, `partition`, `interleave`
- `assoc-in`, `get-in`, `update`, `update-in`, `merge`
- `range`, `repeat`, `repeatedly`
- `some`, `every?`, `not-any?`, `not-every?`
- `into`, `frequencies`, `group-by`
- Multi-arity `defn` (dispatch on arg count)

### Readable Printing & String Functions — COMPLETE
- REPL prints in readable mode (strings with quotes, chars with `\`)
- `pr-str`, `prn` readable printing natives
- Strings as sequences: `first`/`rest`/`nth`/`count`/`empty?`/`map` work on strings
- String functions: `subs`, `str/upper-case`, `str/lower-case`, `str/trim`,
  `str/join`, `str/split`, `str/includes?`, `str/starts-with?`, `str/ends-with?`,
  `str/replace`
- `char?` predicate, `is_string()` C helper

### Cooperative Multitasking — COMPLETE
- Task (TYPE_TASK), Channel (TYPE_CHANNEL) heap types
- Scheduler with instruction-countdown auto-yield (quota=1000)
- `spawn`/`yield`/`await` special forms + VM opcodes
- `chan`/`>!`/`<!`/`close!`/`task?`/`channel?` natives
- Buffered and unbuffered (rendezvous) channels
- REPL scheduler drain after each expression

### Float Type — COMPLETE
- TAG_FLOAT immediate value (double, NaN-boxed)
- Mixed arithmetic promotion (fixnum/bigint + float → float)
- `(/ 5 2)` → `2.5` (non-exact integer division returns float)
- `quot`/`float?`/`int?`/`float`/`int` natives

### Callable Non-Functions — COMPLETE
- Keywords, hashmaps, and vectors callable in head position (IFn-like)
- `(:key map)`, `({:a 1} :key)`, `([10 20] 1)`
- VM-level dispatch in OP_CALL/OP_TAIL_CALL, zero overhead for normal calls

### Additional Standard Library — COMPLETE
- `comp`, `partial`, `juxt` function combinators
- `max`, `min`, `abs` numeric functions
- `sort`, `sort-by` (merge sort), `flatten`, `distinct`, `select-keys`

### TCP Sockets — COMPLETE
- `tcp-listen`, `tcp-accept`, `tcp-connect`, `tcp-local-port` natives
- `beer.tcp` wrapper library with `listen`, `accept`, `connect`, `local-port`
- Integrates with async I/O reactor for non-blocking socket operations

### JSON & HTTP — COMPLETE
- `beer.json` — pure beerlang JSON parser and emitter
- `beer.http` — Ring-inspired HTTP server library
- Middleware support (`wrap-content-type`)

### Bytecode Metaprogramming — COMPLETE
- `disasm` native: disassemble function bytecode to data structure
- `asm` native: assemble data structure to executable bytecode function
- Labels, constants remapping, all opcodes supported

### `read-string` & `eval` — COMPLETE
- `read-string` native: parse one form from a string
- `eval` native: compile-and-run single form (temp VM pattern)
- `keyword` and `name` natives

### Immortal Function Templates — COMPLETE
- `REFCOUNT_IMMORTAL` prevents use-after-free on shared function constants
- Functions without captures share the template object (`PUSH_CONST` instead of `MAKE_CLOSURE`)
- Templates live forever in `load_constants[]`/`load_units[]` arrays

### Atoms — COMPLETE
- `atom`, `deref`/`@`, `reset!`, `swap!`, `compare-and-set!`, `atom?`
- Mutable reference type for managed state
- `beer.test` and `beer.hive` refactored to use atoms instead of `re-def`

### Docstrings & Metadata — COMPLETE
- `meta`, `with-meta`, `alter-meta!` natives for var/function metadata
- `defn` and `defmacro` support optional docstrings after the name
- `doc` macro for printing documentation
- `__print-doc` native for formatted output

### Persistent Vectors — COMPLETE
- Clojure-style 32-way bit-partitioned trie + tail (`src/types/vector.c`),
  new internal object type `TYPE_VEC_NODE = 0x82`; public C API unchanged
  except `vector_pop` now returns an owned reference (was a use-after-free)
- Persistent `vector_conj` / `vector_assoc_n` / `vector_pop_persistent`;
  O(1) `vector_clone`; in-place builder ops guarded by top-down uniqueness
- Beerlang: `assoc` on vectors, new `pop`, `peek`, `subvec`; `update`,
  `assoc-in`, `update-in` now work on vectors via `assoc`
- `into []` of 200k elements: 35.2s → 0.25s; small-vector reads unchanged
- Fixed: `value_hash` now hashes `=` lists/vectors alike (and empty seqs
  like nil); `beer_length` on strings called `vector_length`
- Tests: trie depth boundaries, persistence, top-down sharing trap,
  pop-to-empty across depth-3, heap-element refcounts, differential fuzz;
  ASAN/UBSan clean
- **Follow-ups:** sets on the HAMT (`#{}` reads as `(hash-set ...)` but
  nothing defines it); RRB trees for O(log N) `subvec`/concat;
  user-facing transients

### O(n) list builders in `lib/core.beer` — COMPLETE
- `range`, `map`, `filter`, `take`, `take-while`, `butlast`, `repeat`,
  `repeatedly`, `partition`, `mapcat`, `interleave`, `flatten`, `distinct`,
  `group-by` used `(concat acc (list x))`, copying the accumulator every
  step. Now they cons in reverse and flip once (`__rev`); return types and
  order unchanged.
- `rest` on a vector copies the remainder, so walking a vector input was
  also O(n²) (`reduce`, `map`, `some`, `every?`, `last`, `zipmap`, ...).
  Loops now start from `(__seq coll)` (native, vector → list once).
  `drop`/`drop-while` convert only once something is actually dropped, so
  `(drop 0 v)` still returns `v`. `(drop -1 xs)` now returns `xs` (was `()`).
- Workload of 11 ops over 20k elements: 154.3s → 0.66s, identical output.
- Remaining: a real vector seq type would make `rest` on vectors O(1)
  without the up-front conversion.

### `println`/`str` on collections print type names — TODO
`(println [1 2 3])` prints `vector`, `(str {:a 1})` prints `hashmap` in
script mode (REPL printing and `prn` are correct). Pre-existing, found
while testing persistent vectors.

### ByteBuffer (`beer.bytes`) — COMPLETE
- `TYPE_BYTEBUFFER` heap type: mutable, non-UTF8-validated binary buffer,
  NIO-style position/limit cursor (`src/types/bytebuffer.c`)
- Driven by beeros driver work (virtio-blk/virtio-input/virtio-gpu need a
  real mutable byte type) — full design in `docs/beerlang-bytebuffer.md`
  in the beeros repo
- Construction/cursor: `alloc`, `from-string`, `capacity`, `position`/`!`,
  `limit`/`!`, `remaining`, `rewind!`, `clear!`, `flip!`
- Absolute accessors (bounds-checked vs. capacity): `u8`/`i8` and
  `u16/u32/u64` × `le`/`be`, signed + unsigned, plus `!` setters
- Relative accessors (bounds-checked vs. limit, advance position):
  `get-`/`put-!` for u8/u16le/u16be/u32le/u32be/u64le/u64be
- Bulk: `fill!`, `copy!` (buffer↔buffer), `blit-from-addr!`/`blit-to-addr!`
  (raw pointer↔buffer, same trust model as CFFI), `slice` (always a copy —
  no aliasing in v1), `->string`/`->string-lossy`, `hex`
- DMA: `addr` — raw pointer to `data[0]`
- No opcode/compiler/reader changes — pure native-function surface
- `tests/types/test_bytebuffer.c`; verified end-to-end at the REPL
- **Not yet wired**: beeros's `mem/addr-of` dispatching on ByteBuffer
  alongside String — follow-up once beeros driver work (post-filesystem)
  actually needs it

---

## Near-term TODO (priority order)

### 1. I/O System — Phase 1: Blocking fd-based Streams — COMPLETE

Implemented the Stream abstraction with blocking semantics. The struct and
beerlang-level API are designed for async from day one — only the C internals
use blocking calls. When the cooperative scheduler arrives, we swap the blocking
`read()`/`write()` for reactor-driven non-blocking + task yielding, with zero
changes to user-facing code.

**What stays the same when async arrives:**
- `Stream` struct layout (fd, buffers, type tag, readable/writable flags)
- All beerlang functions: `open`, `close`, `read-line`, `write`, `slurp`, `spit`, `with-open`
- `*in*`, `*out*`, `*err*` dynamic vars
- Buffering layer (read/write buffers are needed in both modes)

**What changes internally for async (later):**
- Add `waiting_readers`/`waiting_writers` queues to Stream
- `stream_read`/`stream_write` check buffer, then yield task instead of blocking
- Register fds with reactor (epoll/kqueue) instead of blocking on `read()`
- Reactor thread wakes tasks on I/O readiness

**Phase 1 tasks (blocking):**
- [ ] `Stream` heap object: `TYPE_STREAM`, fd, type tag, read/write buffers, flags
- [ ] `stream_from_fd()` — wrap an existing fd
- [ ] Buffered read/write (8KB default buffers)
- [ ] File I/O natives: `open`, `close`, `read-line`, `write`, `flush`
- [ ] `slurp` / `spit` convenience functions
- [ ] `with-open` macro in core.beer (auto-close via try/catch, no `finally` needed)
- [ ] Standard streams: `*in*`, `*out*`, `*err*` as dynamic vars
- [ ] Redirect `println`/`print`/`prn` to use `*out*` stream
- [ ] Binary read/write: `read-bytes`, `write-bytes` (needed for tar, network later)
- [ ] Socket support: `connect`, `listen`, `accept`, `close` — same Stream type
  - TCP client: `(def s (connect "host" port))` → Stream
  - TCP server: `(def srv (listen port))`, `(accept srv)` → Stream
  - Sockets are just fds — same `read-line`/`write`/`close` as files
- [ ] `select` / `poll` wrapper for multiplexing (blocking `poll()` for now,
  replaced by reactor later)

**Design decisions:**
- Stream wraps a Unix fd. Files, sockets, pipes, stdin/stdout all use the same type.
  This means `(read-line (open "file.txt" :read))` and `(read-line client-socket)`
  are identical from beerlang's perspective.
- Blocking is acceptable for Phase 1 because there's no scheduler yet — the single
  execution thread would block anyway. The important thing is that the API doesn't
  expose blocking vs non-blocking; it's an implementation detail.
- Sockets go in Phase 1 because they're just fds. `connect`/`listen`/`accept` are
  thin wrappers around POSIX calls. No need to wait for the reactor.
- No `finally` yet, so `with-open` uses `(try body (catch e (do (close f) (throw e))))`
  plus close on success. Works correctly, just verbose in the macro expansion.

### 2. `ns` Macro + `require` — COMPLETE

- [x] `beer.core` as base namespace (all natives + macros defined there)
- [x] `OP_LOAD_VAR` fallback: current ns → function's home ns → `beer.core`
- [x] Qualified symbol resolution (`foo/bar`): alias lookup + namespace resolution
- [x] Namespace aliases: `namespace_add_alias()`, `namespace_resolve_alias()`
- [x] `in-ns` native (create ns if needed, switch current)
- [x] `require` native with `:as` alias support
- [x] `*load-path*` var (default `["lib/"]`), `*loaded-libs*` tracking
- [x] `ns` macro in core.beer: `(ns foo.bar (:require [baz :as b]))`
- [x] Path resolution: `my.ns.data` → `my/ns/data.beer`
- [x] Functions store defining namespace (`ns_name` field) for correct var resolution
- [x] `vm_error` now copies messages (safe for stack buffers)
- **Not yet implemented:**
  - `:refer` in require (only `:as` supported)
  - Circular require prevention
  - `*ns*` dynamic var
  - `BEERPATH` env var

### 3. I/O System — Phase 2: Async Reactor — COMPLETE

- [x] Reactor thread with kqueue (macOS) / epoll (Linux)
- [x] Completion queue for reactor → scheduler communication
- [x] Non-blocking streams with `O_NONBLOCK` on file fds
- [x] `native_blocked` flag with `OP_CALL`/`OP_TAIL_CALL` retry logic
- [x] Tasks block on I/O and wake when data is available
- [x] Guard against concurrent stream access from multiple tasks
- [x] Standalone VMs (no scheduler) fall back to blocking I/O
- **Not yet implemented:**
  - `with-timeout` for I/O operations
  - `select` for multiplexed I/O

---

## Medium-term TODO

### 4. Library Distribution (tar-based) — COMPLETE
- [x] Namespace-to-path convention: `my.namespace.data` → `my/namespace/data.beer`
- [x] Read `.beer` files from tar archives (library bundles)
- [x] C-based ustar tar parser (`tarindex.c`) — transparent indexing at startup
- [x] `BEERPATH` environment variable (colon-separated dirs, scanned for `.tar` files)
- [x] `beer.tar` namespace: `tar/list`, `tar/read-entry`, `tar/create`
- [x] `beer build` / `beer ubertar` CLI commands for creating distributable tars
- [x] Both directory and tar loading supported (dirs for dev, tars for distribution)

### 5. Tooling — PARTIALLY COMPLETE

**Completed:**
- [x] `beer` CLI with subcommands (`new`, `run`, `build`, `ubertar`, `repl`)
- [x] `beer.edn` project configuration (parsed by the reader — it's a beerlang map literal)
- [x] `beer new <name>` — creates project skeleton
- [x] `beer run` — requires main namespace and calls `-main`
- [x] `beer build` — collects `.beer` files into a `.tar` archive
- [x] `beer ubertar` — standalone tar with dependencies
- [x] `beer.shell/exec` native — fork/exec/pipe, returns `{:exit :out :err}`
- [x] `beer.tools` library — build/ubertar logic in pure beerlang
- [x] `beer.test` framework (`deftest`, `is`, `testing`, `run-tests`)

**Remaining:**
- [ ] REPL enhancements: line editing, history, tab completion
- [ ] Debugger: breakpoints, stepping, stack inspection
- [ ] Profiler: sampling or instrumentation-based
- [ ] Documentation generation (docstrings → HTML/markdown)
- [ ] **CLI redesign** — see note below

**Design notes:**
  - Line editing: simplest approach is `rlwrap beerlang` (zero code changes). Built-in
    editing (linenoise or raw terminal) only if needed later.

**CLI redesign (in progress)**

Subcommand logic (`new`, `run`, `build`, `ubertar`) has been moved out of C into
`beer.tools` (pure beerlang). The C binary is now a thin dispatcher. The next step
is to remove even that dispatch from C and move it entirely into the `beer` shell
script, so the binary has zero knowledge of subcommands:

```
beer new myproject
  → beer (shell script): sees 'new', calls: beerlang -e "(require 'beer.tools) (beer.tools/new-project \"myproject\")"
  → beerlang binary: evaluates; no subcommand awareness needed

beer run
  → beer (shell script): sees 'run', calls: beerlang -e "(require 'beer.tools) (beer.tools/run)"
  → beerlang binary: evaluates; beer.tools/run reads beer.edn, sets up paths, calls -main
```

Adding a new subcommand would then mean: add a function to `beer.tools` + add a
`case` in the `beer` shell script — zero C changes.

**Open question:** passing subcommand args (e.g. project name for `beer new`, alias
for `beer run :test`) through inline `-e` string interpolation is fragile for names
containing quotes or spaces. Possible solutions:
- Write args to a temp file, read inside beerlang
- Add a `-a arg` flag to the binary for passing through structured args
- Require subcommand args to be valid beerlang identifiers (project names already are)

Defer until dependency/alias support in `beer.edn` clarifies what arg shapes are needed.

### 6. AOT Compilation
Ahead-of-time compilation to bytecode — skip the reader+compiler at runtime.

- [ ] Serialize compiled bytecode to file (`.beerc` files)
- [ ] Load pre-compiled bytecode directly (skip parse + compile)
- [ ] Cache compiled files (recompile only if source changed, like Python's `.pyc`)
- **Design questions:**
  - Bytecode file format: header (magic, version, checksum) + constant pool + code bytes?
  - How to handle macros? Macros must be available at compile time, so AOT needs
    a two-pass approach or dependency ordering.
  - Should AOT produce a single monolithic file or one `.beerc` per namespace?
  - Worth doing before or after the tar-based distribution?
    Tar bundles could contain `.beerc` files instead of `.beer` source.
  - Native compilation (C codegen or LLVM) is a separate, much larger effort — punt to v2.0+

### 7. Embeddable C API
- [ ] `BeerState*` opaque handle
- [ ] `beer_eval(state, source)` — evaluate a string
- [ ] `beer_register_native(state, name, fn)` — register C functions
- [ ] `beer_call(state, fn, args)` — call a beerlang function from C
- [ ] `beer_get/set` for values

### 8. CFFI (C Foreign Function Interface)
- [ ] Call C shared libraries from beerlang (`dlopen`/`dlsym`)
- [ ] Type marshalling: beerlang values ↔ C types (int, double, char*, structs)
- [ ] Callback support (pass beerlang fn as C function pointer)
- **Design questions:**
  - Use libffi for calling conventions, or hand-roll for common platforms?
  - How to describe C function signatures from beerlang? (Clojure-style type hints? Schema maps?)
  - Thread safety: C calls may block — run on separate thread like `beer.shell/exec`?
  - Memory management: who owns strings/buffers crossing the boundary?

### 9. `task-watch` — Task Completion Monitoring — COMPLETE
- [x] `(task-watch task callback-fn)` — register a callback invoked when task finishes/crashes
- [x] Callback receives task result (or error map on failure)
- [x] Callback is spawned as a new task (runs in scheduler, can do channel ops, spawn, etc.)
- [x] Implementation: watcher list on Task struct, scheduler checks on TASK_DONE transition

### 10. `beer.hive` — Distributed Actor Library — Phase 2 COMPLETE

Erlang-inspired distributed computing for beerlang. Design goal: pure beerlang library with minimal VM changes.

**Phase 1 — Local actors: COMPLETE**
- [x] Actor abstraction (task + mailbox channel), `hive/spawn-actor`, `hive/send`, `hive/receive`
- [x] `hive/ask` / `hive/reply` — request-reply pattern with envelope wrapping
- [x] `hive/register` / `hive/whereis` — actor name registry
- [x] `hive/supervisor` — supervisor trees with `:one-for-one` strategy
- [x] Refactored to use atoms instead of re-def for mutable state

**Phase 2 — Distribution: COMPLETE**
- [x] `beer.hive.wire` — length-prefixed EDN frame protocol
- [x] `beer.hive.node` — TCP node management, connection pool, per-connection reader tasks
- [x] HMAC-SHA256 challenge-response authentication (`beer.digest`)
- [x] `hive/start-node!` / `hive/stop-node!` / `hive/connect-node!` public API
- [x] `hive/ask` routing: local → mailbox, remote → TCP wire → reply channel
- [x] Graceful shutdown: BYE frame, listen-stream close wakes blocked accept task
- [x] C runtime fix: `native_close` wakes blocked I/O tasks; `tcp/accept` handles closed fd
- [x] Smoke tests: 9 two-node loopback tests (all passing, 445 total)

**Remaining phases:**
- **Phase 3 — Resilience:** monitoring, heartbeats, reconnection, cross-node supervisors
- **Phase 4 — Security:** eval sandboxing, resource quotas

**Security concerns:**
- Remote `eval` is powerful but dangerous — needs auth + allowlist/sandbox
- `read-string` of untrusted input — watch for reader bombs (deep nesting, huge strings)
- Resource exhaustion via remote spawn — need per-node quotas
- Network partitions — split-brain detection strategy

---

## Long-term TODO

### 11. Rename/Fork to `clauj`
- ~304 occurrences to rename (purely mechanical)
- Separate GitHub repo
- Do after reaching a "complete" release

### 12. "This would be fun" — C → Beerlang backend
Not a real plan, just an itch to maybe scratch someday. Idea: compile a
restricted subset of C down to beerlang, targeting either the VM's bytecode
directly or an LLVM IR frontend feeding a custom backend.

**Not a CFFI replacement** — different axis entirely. CFFI is the low-level
work force: it calls into real, already-compiled C (GMP, ncurses, whatever)
across a boundary. This idea is about running C *source* as beerlang, with
no boundary at all. The two are complementary, not competing.

- **Full LLVM IR backend is likely not worth it** — LLVM backends assume a
  conventional target: flat memory, fixed-width registers, static types,
  load/store semantics. Beerlang's VM is dynamically-typed, refcounted,
  tagged-value, arbitrary-precision-by-default. Every pointer becomes a GC'd
  heap object, every arithmetic op needs overflow-to-bigint boxing checks —
  closer to "target WASM from LLVM" but without WASM's flat/static memory
  model to lean on.
- **More realistic version**: a small transpiler for a restricted C subset
  (no raw pointer arithmetic, no unions, fixed type set) straight to
  beerlang source or bytecode — Emscripten-flavored, not backend-flavored.

## Known Issues

- **Memory leak warnings at REPL shutdown** — expected (kept CompiledCode alive for fn pointers; intern tables not freed)

## Branch Notes

- **`compiler-destructuring`** — preserves the C-based `let` destructuring approach (superseded by macro approach on `main`)



## NVlass TODO

### Multi arity `keyword`
Should also accept namespace (?) -- also namespace qualified keywords should be printed with namespaces
