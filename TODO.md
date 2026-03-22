# Beerlang Implementation TODO

> **Note:** Remember to update PROGRESS.md when completing major milestones!

## Implementation Status Summary

**All tests passing, 100% pass rate**
- Unit tests: 81
- REPL smoke tests: 340
- **Last Updated:** 2026-03-22

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

### 3. I/O System — Phase 2: Async Reactor

Integrate I/O with the cooperative scheduler (already implemented).
The Stream API is unchanged; we add async machinery underneath.

- [ ] Reactor thread(s): epoll (Linux) / kqueue (macOS)
  - Register stream fds for read/write readiness
  - Wake blocked tasks when I/O ready
  - Completion queue for reactor → scheduler communication
- [ ] Retrofit Stream: add `waiting_readers`/`waiting_writers` task queues
- [ ] `stream_read`/`stream_write` yield task instead of blocking
- [ ] Set fds to `O_NONBLOCK` when reactor is active
- [ ] Channels: `(chan)`, `(>! ch val)`, `(<! ch)` — CSP-style communication
- [ ] `select` becomes reactor-backed (no more blocking `poll()`)
- [ ] `with-timeout` for I/O operations

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

**Design notes:**
  - The CLI is the same `beerlang` binary with subcommands. Project management is built into
    the language itself — no external tool (like leiningen, cargo, etc.) is required.
  - Line editing: simplest approach is `rlwrap beerlang` (zero code changes). Built-in
    editing (linenoise or raw terminal) only if needed later.

**Future consideration: separating project management from the core binary**
  - Currently `new`, `run`, `build`, and `ubertar` are implemented in C in `main.c`.
    An alternative approach would move them into a beerlang library (`beer.tools`)
    that the CLI simply delegates to, similar to how Clojure's `clj` wrapper invokes
    `clojure.main` and `clojure.tools.deps`.
  - What this would look like:
    - `beer.tools.new/create-project` — generate skeleton (uses `shell/exec` for mkdir
      or a new `beer.fs` namespace with `mkdir`, `file-exists?`, etc.)
    - `beer.tools.project/read-config`, `prepend-load-paths` — move beer.edn parsing to beerlang
      (currently C `read_beer_edn()`, but `read-string` + `slurp` already exist)
    - `beer.tools.runner/run-main` — `(require main-ns) (apply (resolve (symbol main-ns "-main")) args)`
    - The C side would only need: parse argv → find beer.edn → bootstrap load path → eval one form
  - Trade-offs:
    - **Pro:** More self-hosted, easier to extend, dogfoods the language
    - **Pro:** Project management becomes customizable via beerlang code
    - **Con:** Slower startup (need to compile/load beer.tools before doing anything)
    - **Con:** Chicken-and-egg: beer.tools needs to be on the load path to load it
    - **Con:** `beer new` currently doesn't even need runtime init — pure C is fast and dependency-free
  - A middle ground (like Clojure): keep the binary minimal (`beer <file>`, `beer -m ns`,
    `beer repl`) and let project management be a library that's invoked via
    `beer -m beer.tools.cli new myproject`. The binary only needs to know how to
    find and run a `-main` function — everything else is library code.
  - **Decision: defer.** Current approach works well. Revisit when the language has
    a richer filesystem API (`beer.fs`) and startup time is optimized.

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

---

## Long-term TODO

### 8. Rename/Fork to `clauj`
- ~304 occurrences to rename (purely mechanical)
- Separate GitHub repo
- Do after reaching a "complete" release

## Known Issues

- **Memory leak warnings at REPL shutdown** — expected (kept CompiledCode alive for fn pointers; intern tables not freed)

## Branch Notes

- **`compiler-destructuring`** — preserves the C-based `let` destructuring approach (superseded by macro approach on `main`)
