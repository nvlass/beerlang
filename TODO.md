# Beerlang Implementation TODO

> **Note:** Remember to update PROGRESS.md when completing major milestones!

## Implementation Status Summary

**All tests passing, 100% pass rate**
- Unit test suites: 26 (`make test`)
- REPL smoke tests: 676 (`bash tests/smoke_test.sh`)
- **Last Updated:** 2026-10-19

## Open Items at a Glance

Small / concrete:
- beeros requests still open: `beer run app.tar`, tar index from memory,
  `*fs*` for `open`/`beer.tar` — see "beeros requests" below
- `::kw` isn't auto-resolved (reads as a keyword named `:kw`). Resolving
  at read time needs `load`, the file runner and `beerc` to read
  form-by-form instead of `reader_read_all` up front, or the file's
  `(ns ...)` hasn't run yet
- Maps aren't seqable: `seq`/`map`/`reduce`/`doseq` over a map fail
  (`reduce-kv` works)
- No string output stream: `with-out-str`, and `:out` messages in the EDN
  nREPL (whose sessions bind `*ns*` but not `*out*`), need one
- Destructuring is vectors only: no map destructuring (`{:keys [a]}`) in
  `let` or `fn`; `loop` doesn't destructure
- `make asan` races under `-j` (`asan: clean all` runs clean in parallel);
  run it as `make asan -j1` or twice
- stdin/stdout/stderr stay blocking fds: `(read-line *in*)` inside a task
  still blocks the thread
- Floats print with `%g` (6 significant digits) in `str`/`pr-str`/REPL:
  `(str 3.14159265)` → `"3.14159"`, `-250.0` prints as `-250`. `json/emit`
  uses shortest round-trip formatting; the core printer should too
- String literals don't support `\uXXXX` escapes in the reader
- `(str coll)` / `pr-str` of collections go through `open_memstream` +
  `value_print_readable` (`value_sprint_readable` in value.c): a stream
  per call. Fine for now; worth a direct buffer printer if it shows up
- `apply`, and natives that call back into beerlang (`swap!`, `reduce-kv`,
  sort comparators, ...), run the fn in a temp VM: inside it I/O can't park
  (it would park the *outer* task while apply returns) and nothing yields.
  Multi-arity `defn` no longer goes through apply; a real fix needs a
  re-entrant call path in the VM (native asks the VM to call a fn and
  resume it with the result)
- `beer.http` gaps: no `Date` header (`current-time-millis` exists now; needs
  HTTP-date formatting, e.g. a `gmtime`-based native), no limit on
  request-line / header-line length (`read-line` is unbounded), no chunked
  request bodies (501), request bodies must be UTF-8 text

Performance — investigate / optimize:
- VM dispatch, done 2026-10-17 (macOS empty-loop 173 -> 101 ns, fib 20
  6.9 -> 4.0 ms; Linux empty-loop 108 -> 42 ns): inline retain/release,
  operand reads, push/pop and hot accessors; step() inlined into vm_run's
  loop; yield countdown at safe points (jumps, fn entry).
- Tried and reverted (65d019f): one dispatch function where every opcode
  ends in NEXT (fetch + dispatch), with optional computed goto. Neutral on
  Apple Silicon + clang; on Linux + GCC 20-30% slower with the switch and
  still slower with computed goto on closures, atoms, tasks and pure JSON
  (`make bench REF=8d9f304` vs `REF=65d019f [EXTRA_CFLAGS=-DVM_THREADED_DISPATCH]`).
  Untested guess: GCC stops inlining helpers into the much larger function
- [investigate] keep `pc`/`sp`/`code` in locals in the dispatch loop; write
  back only around calls into C, yields and errors. Measure on both
  platforms (the dispatch rewrite above was a regression on one)
- Multi-arity fns are native (done 2026-10-17): one Function with an arity
  table, picked by argc in CALL/TAIL_CALL; `multi-arity-call` 382 -> 189 ns.
  Side effect: +1-3% on other benchmarks (code layout, measured with
  `make bench REF=`), check on Linux
- Profile of 23c6ce8 (macOS/clang, Instruments; empty loop / fib / pure JSON
  parse): dispatch is 33% / 27% / 27% of all time, CALL 25-29% including the
  natives it runs, LOAD_VAR 12-15%. Cheap follow-ups:
  - [done] running/error/yielded share one word, tested with one compare;
    bytecode verifier (verify.c) lets release builds drop per-instruction
    pc/operand/opcode/constant checks: -25% to -35% on VM-bound benchmarks.
    Opcode tracing is debug-build-only now. Next candidates: stack-depth
    verification (would drop push/pop and local-index checks)
  - [todo] force-inline push_value/pop_value: clang keeps an out-of-line copy
    that costs 5-6%
  - [investigate] Register caching through a Regs struct passed into the
    inlined step(): phase 1 (pc/code, on main since 23c6ce8) was 1-4% faster
    on clang but 3-8% slower on GCC; phase 2 (sp/stack, branch
    vm-sp-registers, 6ae2029, not merged) was mixed on clang and +6% to +19%
    slower on GCC. Likely GCC keeps the struct in memory inside the huge
    function. Next: try plain locals in one function (no struct pointer),
    check GCC's output with `perf annotate`, and measure reverting phase 1
    on GCC
- [investigate] Inline core fns at compile time: `OP_ADD`/`OP_LT`/… exist in
  the VM but the compiler never emits them, so `(+ k 1)` is LOAD_VAR + CALL
  of a native. Clojure inlines via `:inline` metadata on core vars
  (arithmetic, comparisons, `inc`/`dec`, `zero?`/`pos?`/`neg?`, `nth`,
  `get`, `count`, bit ops, casts, `aget`/`alength`, …), and with direct
  linking compiled call sites don't see later redefinitions. Decide whether
  beerlang takes the same trade (today's lookup is fully late-bound;
  see the LOAD_VAR cache smoke tests)
- [discuss] Branches / "jump misses": the 6–8 conditional checks per opcode
  before real work, and a single shared `switch` dispatch point that
  predicts badly. Plus bytecode-level waste seen in `disasm` of a `loop`:
  a dead `JUMP :L2` right after `JUMP :L0`, and `JUMP_IF_FALSE` that
  doesn't pop, so both branches start with `POP`

Bigger:
- AOT: `require`/`load` don't use `.beerc` yet (format + compiler exist) — #6
- CFFI callbacks (C calling a beerlang fn) — #8
- `beer.hive` Phase 4 (security) — #10
- Tooling: line editing/history, debugger, profiler, doc generation,
  CLI redesign phase 2 — #5
- I/O: `with-timeout`, `select`/`alts` — #3
- Vectors: transients, RRB trees, a vector seq type — persistent vectors section
- `examples/embedded_http_prefork` (multi-core HTTP example, one process
  per worker) — designed in `examples/embedded_http/README.md`
- Pick a concurrency direction from
  `docs/design/14_WORLD_ISOLATION_INVESTIGATION.md`; reconcile
  `docs/design/08_MULTITASKING.md` with the scheduler as built

Long-term: `clauj` (#11), C → beerlang (#12), the Doom rewrite (needs
beeros filesystem + input driver; see the beeros roadmap)

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

### Refcount fixes: closures, tail calls, spawn, embedding — COMPLETE
Found via a crash in `examples/embedded_http` under `wrk` load. Several
bugs masked each other, so they had to be fixed together:
- `OP_MAKE_CLOSURE` never released its allocation reference after
  `vm_push`, and kept an extra retain on every captured value: every
  closure with captures leaked, together with everything it captured.
- That leak hid an over-release in `OP_TAIL_CALL`: when the new call has
  more args than the old frame had slots, slots already moved were
  released again. Its `fn` stack reference also leaked. Rewritten to
  release exactly `[frame_base, args_start)`, then move args down, with
  the frame inheriting the stack's reference to `fn`.
- It also hid that `task_new` borrowed `fn`/args without retaining them:
  a spawned task runs after the spawner drops its references. Tasks now
  retain what they store (`owns_constant_values`). The task-watch
  workaround that compensated for the old borrow was removed.
- `task_destroy` freed `vm->code`/`vm->constants`, which the VM swaps on
  every call, so a task ending inside a callee freed someone else's
  arrays: the pre-existing double frees in `task_destroy` and
  `compiled_code_free` seen under ASAN. Tasks now record their own arrays.
- `beer_call` leaked its constants array and the retains on fn/args, so
  every embedded call leaked its whole argument (the request map).
- `beer_release` is now a no-op on interned symbols/keywords, and `beer.h`
  documents that `beer_keyword`/`beer_symbol` return borrowed values: the
  example released them, freeing live interned keywords.
- Regression test: `tests/runtime/test_refcount.c` (fails on the old code).
- Result: `examples/embedded_http` is ASAN-clean under load, live heap flat
  across 45k requests (was 18 leaked blocks per request).

**Done since:** interned symbols/keywords are now immortal
(`REFCOUNT_IMMORTAL`, set in `intern_value`; `symbol_shutdown` restores a
normal count to free them), so a stray retain/release on one is a no-op.
That removes this whole bug class, including a pre-existing shutdown-only
use-after-free (`read-string` returned a borrowed symbol that
`task_destroy` released). The `beer_release` special case is gone again.
The full smoke suite under ASAN now reports no memory errors.
`MEMORY_MODEL.md` no longer documents the nonexistent
`value_release`/`value_retain`.

**Follow-ups found along the way (all fixed 2026-10-10):**
- Functions and native functions stored their arity in `header.size`,
  which the free path uses for byte accounting ("Bytes still allocated"
  at exit underflowed). Arity now has its own field.
- Makefile now tracks header dependencies (`-MMD -MP`).
- UBSan: signed left shift in `read_int64` (`src/vm/vm.c`).

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
- **Follow-ups:** RRB trees for O(log N) `subvec`/concat; user-facing
  transients; a vector seq type so `rest` on a vector doesn't copy (sets:
  done, see "Sets")

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

### `println`/`str` on collections — COMPLETE
- `value_sprint` (`src/runtime/core.c`) fell back to the type name for
  collections: `(println [1 2 3])` printed `vector`. It now renders lists,
  vectors, maps and sets in display mode, recursively, matching Clojure:
  `(println ["a" \b])` → `[a b]`. Functions, atoms, etc. render as the
  REPL shows them (`#<fn ...>`) instead of a bare type name.
- `str` renders a collection argument readably, like Clojure:
  `(str ["a"])` → `"[\"a\"]"`; a top-level string or char stays raw.

### `compare` and comparator-based `sort` — COMPLETE
- Native 3-way `compare` with Clojure's ordering: nil first; numbers across
  fixnum/float/bigint; false < true; chars by codepoint;
  strings/keywords/symbols lexicographically; vectors by length then
  element-wise. Incomparable types are an error.
- `sort` and `sort-by` default to `compare` (strings, keywords etc. now
  sort; before, `sort` used `<`, numbers only). A comparator may be 3-way
  or a boolean predicate (`(sort > xs)` still works), as in Clojure. Added
  `(sort-by keyfn comp coll)`. Stable.
- The merge step cons'ed onto a recursive call, so its depth grew with the
  input; it's now a loop with an accumulator (recursion depth is log n).
  100k elements sort fine.

### Sets — COMPLETE
- New `TYPE_SET = 0x23` (`src/types/set.c`, `include/set.h`), a persistent
  set wrapping the HAMT hashmap (element → itself).
- `#{...}` now works (the reader already produced `(hash-set ...)`).
  Natives: `hash-set`, `set`, `disj`, `set?`; `conj`, `count`,
  `contains?`, `get`, `empty?`, `first`, `rest` handle sets, and `__seq`
  does too, so `reduce`/`map`/`filter`/`into` work on them.
- Sets are callable (`(#{1 2} 1)` → `1`), compare order-independently,
  hash order-independently (usable as map keys), print as `#{...}`, and
  `(type #{})` is `:set`.
- Tests: smoke checks, plus a leak/over-release probe in
  `tests/runtime/test_refcount.c`.

### WASM build and 64-bit integers on wasm32 — COMPLETE
- `wasm/Makefile.wasm` lists runtime sources explicitly, so the new
  `src/runtime/bytes.c` was missing: undefined `core_register_bytes`.
  Added, with a comment that new `src/runtime` files must be listed there
  or stubbed in `wasm/stubs.c`.
- `bigint_from_int64`/`bigint_to_int64` used `mpz_set_si`/`mpz_get_si`/
  `mpz_cmp_si`, which take a C `long` -- 32-bit on wasm32. Overflow
  promotion truncated operands: `(* 3037000500 3037000500)` was wrong in
  the browser REPL. Now uses `mpz_import`/`mpz_export` of a 64-bit word.
- `pr-str`/`str` printed bigints as `#<bigint>` (all platforms; visible on
  the site because the WASM REPL renders results with `pr-str`).
- Tests: full-width int64 round-trip in `test_bigint.c`, bigint smoke
  checks; verified in the Docker-built WASM module under Node.

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
- beeros's `mem/addr-of` now dispatches on ByteBuffer alongside String
  (beeros `90ce99f`)
- **Likely needed: an aligned constructor**, e.g. `(bytes/alloc-aligned n
  align)` with `data[0]` on an `align` boundary. `data[]` sits right after
  the header (36 bytes in), so a plain `alloc` is only ~4-byte aligned, and
  DMA ring memory isn't: virtio descriptor tables need 16-byte alignment,
  other devices want 64 or a page. beeros's beerlang virtio-blk driver
  starts by over-allocating and rounding the address up inside the buffer
  (works, no runtime change, but every access carries an offset). Revisit
  once a second driver needs it. Note: aligning `data[0]` means a variable
  gap between header and data, so `bytebuffer_data()` can't stay a fixed
  offset — e.g. store the data offset in the object.

### beeros requests: file system hook, loading, packages, interrupts

**Boundary (2026-10-18): configure, don't patch — see docs/EMBEDDING.md.**
`beerlang.mk` lists the runtime's sources for every build (host, WASM,
beeros), with `BEER_PLATFORM` (posix/none) and feature switches
(`BEER_NET`, `BEER_SHELL`, `BEER_CRYPTO`, `BEER_AOT`, `BEER_CFFI`; off adds
`-DBEER_NO_<X>` and namespace_init skips it). `src/platform/none/` has the
stub I/O reactor that WASM and beeros used to copy. `core_load_source_ns`
loads a source into a namespace (replaces beeros's `blob_loader.c`). WASM
now builds from beerlang.mk (no hand-listed sources, no `stubs.c`).
beeros migration checklist: docs/EMBEDDING.md §5. Language vs platform:
drivers, `beer.mem`, `beer.gfx`, console/term, fs/ext2/ustar, packages
stay in beeros. Open in beerlang: `*fs*` doesn't cover `open`/streams/
`beer.tar`; `beer.hive` requires `beer.tcp`/`beer.crypto` even for local
actors, so it doesn't load on `BEER_PLATFORM=none`; ByteBuffer raw-address
ops (`addr`, `blit-*-addr!`) could move behind an opt-in.

Context: beeros (bare-metal, `beerlang/` as an unmodified submodule) has
a filesystem written in beerlang (`beer.ext2` + `beer.fs`, mounted at
`/`) but no host libc filesystem: `fopen` finds nothing. Today beeros
works around that by wrapping things on its side, which makes the same
code behave differently on a host and on beeros. The goal of the items
below is to remove those wrappers so beeros stays plain beerlang. Each
item is independent; host builds must behave exactly as now when no hook
is installed. Workarounds currently in beeros (to delete afterwards):
`beer.fs/load-file` (read-string + eval), a `beer.core/require` wrapper
(loads `/lib/foo/bar.beer`, marks `*loaded-libs*`, then calls the native),
and the boot loader marking embedded namespaces in `*loaded-libs*`.

**Done (2026-10-17), option (b): loading moved into `lib/core.beer`.**
- `beer.core/*fs*`, nil by default (host), or `{:read (fn [path]
  string-or-nil) :write! (fn [path s append?])}`. `slurp`, `spit`,
  `load-file`/`load` and `require`'s `*load-path*` search go through it;
  the tar index stays the fallback. beeros: `(def beer.core/*fs* ...)`.
- `require`, `load-file`, `slurp`, `spit` are beerlang fns on small natives
  (`__file-read`, `__tar-read`, `__slurp`, `__spit`, `__current-ns`,
  `__alias!`, `__refer!`). `require` takes Clojure vector specs and flat
  options, several per call; `ns` emits vector specs. `*ns*` is restored
  with `finally` when a load fails; circular requires are detected.
- `load-string` native (`core_load_source`, public for embedders: beeros's
  `beer_load_blob` can use it). Load errors are catchable (`vm_throw_error`)
  and name the file and form; a failing script now exits 1.
- `(def x "doc" v)` attaches the docstring like `defn`.
- `namespace_init_bare()` skips the `lib/core.beer` search (no NOTE).
- `beer build`/`ubertar` store entries relative to their `:paths` root
  (they were `src/foo/core.beer`, so `require` never found them in a built
  tar) and include `beer.edn`.
- beeros can delete: `beer.fs/load-file`, the `beer.core/require` wrapper,
  and probably the `*loaded-libs*` marking for embedded namespaces (still
  needed if those have no file under `*load-path*`).

Still open from item 1: `open`/streams on files, `beer.tar/*` and the
`BEERPATH` tar scan still use libc; `:read-bytes`/`:exists?`/`:list` hook
keys wait for a caller that needs them. `require` now needs `core.beer`
(was native): a runtime without it has no `require`.

4. **Interrupting the running task — done (2026-10-17).** Options taken:
   delivered by the scheduler between quanta (no VM hot-path cost), skips
   `catch` but runs `finally`, foreground task only. `vm_request_interrupt()`
   (signal/IRQ safe) or `(request-interrupt!)` from beerlang; the REPL maps
   Ctrl-C to it (a second Ctrl-C while one is pending exits). New opcode
   `OP_PUSH_FINALLY` marks finally handlers. Blocked foreground tasks
   (channel, sleep, I/O) are woken; nested loads pass it through; temp-VM
   natives (`apply`, `swap!`, ...) check it via `scheduler_run_nested`.
   Design notes in `docs/design/08_MULTITASKING.md` "Interrupts".
   beeros: call `vm_request_interrupt()` from the UART break / keyboard
   chord; its own wait loop must return when `vm_interrupt_requested` is
   set, and clear the flag at its prompt.
   Not covered: a task stuck inside a blocking C call (e.g. `read-line` on
   the still-blocking stdin), and a background `request-interrupt!` while
   the foreground runs inside a temp VM (nothing else runs then).

5. **Tar packages that know their entry point.** beeros wants to run a
   `.tar` the same way `beer run` runs a project:
   - ~~Include `beer.edn` in what `beer build` / `beer ubertar` write~~
     (done); still to do: let `beer run app.tar` read `:main` (and later
     other keys) from it.
     The project file doubles as the package description; no new
     manifest format.
   - Index a tar from memory: `tar_index_add_buffer(idx, name, data,
     len)` or a native `(beer.tar/index! bytebuffer)`, since beeros
     reads the tar from its own filesystem, not through `FILE*`.
   - ~~Entries stored as `src/foo/core.beer`~~: fixed, see above.

6. **Small ones.** `(def x "doc" v)` and the core.beer NOTE are done (see
   above). Open: aligned ByteBuffer constructor, see the ByteBuffer
   section.

7. **Follow-ups from beeros using 9aa8590 — done (2026-10-18).**
   - `load-file`/`load-string` restore the current namespace afterwards,
     also on error (`core_load_source`); `eval` of `in-ns` still sticks.
   - Errors print once: `vm_run` no longer writes `VM Error:`; callers
     report, and a spawned task with no caller prints `Task error: msg`.
   Not beerlang: an interrupt map that turned into nil was beeros's linker
   script overlapping `.bss` with the TLS block (`errno` writes).
   Found on the way: the REPL hung at EOF when stdin was a regular file
   (`beerlang < file`; kqueue never reports EOF there). Fixed.

8. **Several REPLs in one image (2026-10-18).** beeros is adding a
   framebuffer terminal whose REPL runs as a spawned task next to the
   serial REPL (the foreground). Sharing every definition is the point;
   two things are not meant to be shared:
   - **Current namespace per task.** The registry's current ns and
     `beer.core/*ns*` are global, so `(in-ns 'foo)` in one REPL moves the
     other. Clojure gets this from `binding` on `*ns*`; beerlang has no
     dynamic vars yet. Either `binding` (thread-local, i.e. task-local,
     values for `^:dynamic` vars, which would also give `*out*`, `*args*`
     and friends) or, smaller, a per-task current namespace that `in-ns`,
     `*ns*` and compilation read, inherited by `spawn`.
   - **Interrupting a given task.** `vm_request_interrupt()` only reaches
     the foreground task, so a runaway form in the window REPL can't be
     stopped. Wanted: `vm_request_interrupt_task(task)` /
     `(interrupt! task)`, same semantics (skip `catch`, run `finally`),
     delivered between quanta; the foreground variant stays as is.
   - ~~`def` of an unqualified name inside a fn used the namespace
     current when the fn runs~~: done (2026-10-18), it uses the fn's home
     namespace, as in Clojure (qualified `def` aliases resolve there too).
     beeros can drop the `beer.pkg/*args*` qualification.
   Done (2026-10-18): (2) `eval`/`load-string`/`load-file`/`require` run
   each form in the caller's VM (`__compile` + a beerlang loop) instead of
   a child task + nested scheduler loop, which made two REPLs evaluating at
   once stall each other. `eval` splits a top-level `do` and lets thrown
   maps through unchanged (was "eval: ..." text).
   (3) Done (2026-10-18): dynamic vars. `(def ^:dynamic *x* ..)`, `binding`
   (values live in the task, inherited by spawned tasks, so bodies may
   yield), `set!`. `*ns*`, `*out*`, `*in*`, `*err*`, `*fs*`, `*load-path*`
   are dynamic; with `*ns*` bound, `in-ns` changes only the binding and the
   current namespace follows it. nREPL connections bind `*ns*` (the simple
   one also `*out*` to the socket). Reader takes `^` metadata (on def
   names; symbols can't carry it elsewhere). beeros: run the window REPL
   inside `(binding [*ns* 'user *out* term *in* kbd] ...)`.
   (4) Done (2026-10-18): `(interrupt! task)` / `vm_request_interrupt_task`
   (signal/IRQ safe), same semantics as the foreground interrupt;
   `(current-task)`. Fixed on the way: waking a channel-blocked task for an
   interrupt (also the foreground one, since 2526809) enqueued it before
   unlinking it from the channel queue, which corrupts the ready queue
   when other tasks are ready (both queues use next/prev).
   Done too: `(catch :interrupt e ...)` (opt-in, takes only the interrupt;
   new `OP_PUSH_INTERRUPT_CATCH`), so a REPL loop in beerlang survives
   `interrupt!` on its own task: `(try (eval f) (catch :interrupt e ...)
   (catch e ...))`.
   Done (2026-10-18), option B: no temp VMs for fns given to natives.
   `apply` asks the VM to make the call (`vm_request_call`; ~38% faster,
   tail calls through apply, >65535 args); `swap!` (compare-and-set! retry
   loop), `reduce-kv`, `alter-meta!` are beerlang. The old native `swap!`
   returned nil when f yielded. VM stack/frames now grow on demand (were
   256 values / 64 frames: non-tail recursion ~64 deep failed). Only
   macroexpansion still uses a temp VM.

9. **Exception handler stack is fixed at 16 — done (2026-10-18):** it
   grows on demand like the frames (cap 100k); `(deep 5000)` with a
   `try` per level works. require still pushes 3 handlers per nesting
   level, which no longer matters. Also added for beeros input backends
   (docs/input.md there): `offer!` / `poll!`, non-blocking channel put and
   take (core.async names). And freeing a long list no longer recurses
   per cons cell (a 1M-element list crashed the process; ASAN overflowed
   at 20k). Original report: Found in
   beeros running a package from the window REPL: `(beer.pkg/run "hello")`
   failed with `load: error in /pkg/hello.tar/hello/core.beer (form 1):
   load: error in /pkg/hello.tar/hello/fmt.beer (form 1): Exception
   handler stack overflow`.
   - `vm->handlers` is allocated with `DEFAULT_HANDLER_CAPACITY` (16,
     `include/vm.h:92`, `src/vm/vm.c:83`) and `OP_PUSH_HANDLER` /
     `OP_PUSH_FINALLY` / `OP_PUSH_INTERRUPT_CATCH` throw when it's full
     (`src/vm/vm.c:2022`). The value stack and call frames grow on demand
     since 2d89270; the handler stack doesn't. Every dynamically active
     `try` clause holds an entry, so the limit is on `try` nesting depth
     across calls, not per function.
   - `require` alone costs 3 entries per level of nesting: `__load-lib`'s
     `try`/`finally`, `load-string`'s outer `try`/`finally` and its
     per-form `try`/`catch`. `(ns a (:require [b]))` → `b` requires `c`
     ... so a dependency chain 5 deep needs 15 entries before any user
     code, and a library whose forms use `try` themselves needs more.
   - The beeros case: the window REPL's loop held 4 entries (outer
     `try`/`finally`, inner `catch :interrupt` + `catch` + `finally`),
     then `run` → `require hello.core` (3) → `require hello.fmt` (3) →
     the forms inside. Plain `(beer.pkg/run "hello")` fit with ~3 to
     spare; wrapping it in 4 more `try` levels overflowed. Measured:
     `(defn deep [n] (if (= n 0) :ok (try (deep (dec n)) (finally nil))))`
     — on the serial REPL `(deep 15)` works and `(deep 16)` overflows;
     inside the window REPL the limit was 11. beeros 3398c19 cut its
     loop to the two catches (limit now 12), which only buys one level.
   - Host repro: `(deep 16)` at the REPL, or a chain of ~6 libs where
     each `(ns ...)` requires the next.
   - Wanted: grow `vm->handlers` like `grow_frames` (double up to a cap
     such as `MAX_FRAME_CAPACITY`, "Exception handler stack overflow"
     only past that). Nothing should hold a pointer into `vm->handlers`
     across a push (check `pop_handler`, vm.c:283, and the unwinding
     code). Also check that temp VMs (macroexpansion) and task VMs get
     the same growth.

10. **`beer.string/replace` with a shorter replacement — done
    (2026-10-19):** the length delta is computed per direction, a failed
    `malloc` throws "replace: out of memory". Found on the way: an empty
    target returned the input without retaining it (use-after-free on the
    second call). Smoke tests cover both. Also done: the ~80 `malloc`/`calloc`/`realloc`
    results in `src/` that were used without a NULL check now go through
    `xmalloc`/`xcalloc`/`xrealloc` (`include/memory.h`), which abort with
    "FATAL: Out of memory (requested N bytes)" like `object_alloc`;
    `mem_alloc`/`mem_realloc` call them.
    Original report:
    beeros crashed (store fault in `memcpy`, address 0) on
    `(beer.string/replace "a\r\nb" "\r" "")`. `string_replace`
    (`src/types/string.c:465`) computes
    `new_len = s->byte_len + count * (r->byte_len - t->byte_len)`; with
    unsigned lengths `0 - 1` wraps, so the size is ~4 GB per occurrence.
    The host survives because the huge `malloc` succeeds lazily; beeros's
    returns NULL, which the next line writes through (no NULL check).
    Wanted: compute the delta signed (or `s->byte_len - count *
    (t->byte_len - r->byte_len)` when shrinking), check `malloc`, and a
    smoke test replacing with a shorter and an empty string. Worth a scan
    for the same pattern elsewhere (unsigned length differences,
    unchecked `malloc`). beeros works around it with split/join.

11. **`beer.hive` without the network (2026-10-19).** The local half —
    actors, mailboxes, `ask`/`reply`, `register`/`whereis`, supervisors,
    `monitor` — needs no sockets, but `(ns beer.hive (:require
    [beer.hive.node ...]))` pulls in `beer.tcp` and `beer.crypto` at load,
    so `beer.hive` doesn't load at all on `BEER_PLATFORM=none` (beeros,
    WASM). Wanted: `beer.hive` loads with `BEER_NET=0`; the node part
    (`beer.hive.node`, `beer.hive.wire`) is required lazily, e.g. inside
    `start-node!`/`connect-node!`, with a clear error when `beer.tcp`
    isn't there; remote routing in `ask`/`send` only when a node runs.
    A smoke test that loads `beer.hive` and runs local actors with
    `BEER_NET=0` would keep it that way. beeros plans to use the
    registry as its "named channels" (instead of Unix sockets: one image,
    so a name -> channel map is enough).

Related: `::kw` resolution needs form-by-form reading in `load`; a
`load-string` built on `core_load_source` inherits that.

---

## Near-term TODO (priority order)

### 0. Native fast path for `sort` — COMPLETE
Sorting 100k elements took ~6.5s: every comparison went through the
beerlang `__comparator` closure into the native `compare`.

- [x] When the comparator is `compare` — `(sort coll)` or
  `(sort compare coll)` — `__sort-native` sorts in C: elements into an
  array, stable bottom-up merge sort with `compare_values`, back to a list.
  Same result, order and errors as the beerlang path.
- [x] `(sort-by keyfn coll)`: beerlang builds `[key x]` pairs (keyfn once
  per element), `__sort-pairs-native` sorts by key and strips them.
- 100k-element sorts now take a fraction of a second (a whole benchmark
  script with two 100k sorts and a 50k sort-by runs in 0.87s).
- Found while testing: variadic functions leaked their rest-argument list
  (`OP_CALL`/`OP_TAIL_CALL` retained it again after storing it in the
  stack slot), so every call to a multi-arity `defn` — `sort`, `sort-by`,
  ... — leaked its arguments. Fixed; covered by `test_refcount.c`.
- Left for later: a separately named sort (`nsort`/`fsort`) — only worth it
  if it trades something for speed (unstable, numbers-only or in-place on
  a vector/ByteBuffer), with a docstring spelling that out.

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
- [x] `Stream` heap object: `TYPE_STREAM`, fd, type tag, read/write buffers, flags
- [x] `stream_from_fd()` — wrap an existing fd
- [x] Buffered read/write (8KB default buffers)
- [x] File I/O natives: `open`, `close`, `read-line`, `write`, `flush`
- [x] `slurp` / `spit` convenience functions
- [x] `with-open` macro in core.beer
- [x] Standard streams: `*in*`, `*out*`, `*err*` as dynamic vars
- [x] `println`/`print`/`prn` write to `*out*` (resolved current ns →
  `beer.core`, falls back to stdout); `binding` still missing
- [x] Binary read/write: `read-bytes`; `(write-bytes stream buf)` writes a
  ByteBuffer's position..limit and advances position (NIO-style)
- [x] Socket support — as `beer.tcp` (`tcp/listen`, `tcp/accept`,
  `tcp/connect`), same Stream type, same `read-line`/`write`/`close`
- [ ] `select` / `poll` for multiplexing — see #3

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
- [x] Circular require detection (`requiring_stack` in `core.c`)
- [x] `*ns*` dynamic var
- [x] `BEERPATH` env var (see #4)
- [x] (2026-10-12) Unqualified symbols resolve in the running function's home
  ns, then `beer.core` — not the caller's current ns, which made a `user/+`
  redefinition change what `beer.core/inc` called. Closures made by
  top-level code and REPL-defined functions now record their home ns too
- [x] `:refer [syms]` / `:refer :all` in `require` and `ns` — referred
  names share the source Var (kept in a separate `refers` map, so a local
  `def` shadows instead of clobbering)

### 3. I/O System — Phase 2: Async Reactor — COMPLETE

- [x] kqueue (macOS) / epoll (Linux) backend behind `reactor.h`
- [x] Non-blocking streams with `O_NONBLOCK` on file fds
- [x] `native_blocked` flag with `OP_CALL`/`OP_TAIL_CALL` retry logic
- [x] Tasks block on I/O and wake when data is available
- [x] Standalone VMs (no scheduler) fall back to blocking I/O
- [x] (2026-10-10) Reactor folded into the scheduler loop: no reactor
  thread, mutex or completion ring (which dropped tasks past 256). Per-fd
  reader/writer wait lists, so one task can read a socket while others
  write it. The idle scheduler blocks in kevent/epoll_wait until the next
  fd event or timer instead of polling every 1 ms, and the 1M-iteration
  limits (a server stopped after ~16 idle minutes) are gone. The REPL
  waits on stdin through the same poll, so background tasks run at full
  speed at the prompt (was 1 tick per 10 ms)
- [x] (2026-10-10) Writes park: `write`/`write-bytes`/`flush`/`close`/
  `print`/`println`/`prn` park on a full socket buffer (the retry skips
  re-buffering via `vm->write_resumed`); write errors throw instead of
  being ignored; the blocking flush no longer resends bytes after a
  partial write; SIGPIPE is ignored so a dead peer gives EPIPE
- [x] (2026-10-10) `tcp/connect` parks on writability plus a timer for its
  timeout instead of `select()` blocking the thread
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

### 6. AOT Compilation — PARTIALLY COMPLETE
Ahead-of-time compilation to bytecode — skip the reader+compiler at runtime.

- [x] Serialize compiled bytecode to `.beerc` files (`src/lib/aot.c`,
  `lib/beer/beerc.beer`, `lib/beer/build.beer`; format: magic "BEER" +
  version + source mtime + CRC32 + per-form bytecode and typed constants)
- [x] `beer compile` / `beer check` — compile stale files, report stale/missing
- [ ] **Load pre-compiled bytecode in `require`/`load`** when the `.beerc`
  is fresh (skip parse + compile) — the remaining piece
- Known limitation: `compile-file!` is compile-only, so macros defined in a
  file are not available later in the same file.
- Native compilation (C codegen or LLVM) is a separate, much larger effort.

### 7. Embeddable C API — COMPLETE
- [x] `include/beer.h` / `src/lib/beer.c` / `make libbeerlang` →
  `build/libbeerlang.a`: `beer_open`/`beer_close`, `beer_do_string`,
  `beer_do_file`, `beer_eval_expr`, `beer_lookup`, `beer_call`,
  `beer_register`, value constructors/inspectors, `beer_length`/`beer_nth`/
  `beer_get`
- Examples: `examples/embed.c`, `examples/raylib_game`, `examples/embedded_http`
- Gap: no map/vector constructors in `beer.h` (embedders reach into
  `hashmap.h`/`vector.h`, see `examples/embedded_http/main.c`)

### 8. CFFI (C Foreign Function Interface) — PARTIALLY COMPLETE
- [x] Call C shared libraries via libffi (`make CFFI=1`): `ffi/open`,
  `ffi/sym`, `ffi/call`, `ffi/malloc`/`ffi/free`, `ffi/cget`/`ffi/cset!`
- [x] Binding macros: `def-cfn`, `def-cstruct`, `def-cstruct-accessors`,
  `load-bindings`; `scripts/beer-probe` generates multi-ABI bindings
- [ ] Callback support (pass a beerlang fn as a C function pointer)
- Open: blocking C calls still run on the VM thread (could dispatch to a
  separate thread like `beer.shell/exec`)

### 9. `task-watch` — Task Completion Monitoring — COMPLETE
- [x] `(task-watch task callback-fn)` — register a callback invoked when task finishes/crashes
- [x] Callback receives task result (or error map on failure)
- [x] Callback is spawned as a new task (runs in scheduler, can do channel ops, spawn, etc.)
- [x] Implementation: watcher list on Task struct, scheduler checks on TASK_DONE transition

### 10. `beer.hive` — Distributed Actor Library — Phase 3 COMPLETE

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

**Phase 3 — Resilience: COMPLETE**
- [x] Heartbeats (PING/PONG), automatic reconnection with exponential backoff
- [x] `monitor`/`demonitor` with DOWN messages
- [x] Cross-node supervisors
- [x] Smoke tests: start/stop/restart, double-start, monitor/demonitor,
  heartbeat keepalive (the two-node remote-ask tests are occasionally flaky
  on timing)

**Remaining:**
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

- **Memory leak warning at REPL shutdown** — the live object count is
  expected (compiled units are kept alive because functions hold raw
  pointers into their bytecode). The huge "Bytes still allocated" number is
  an accounting artefact: functions store their arity in `header.size`,
  which the free path uses as the allocation size (see the refcount
  section's follow-ups).

## Branch Notes

- **`compiler-destructuring`** — preserves the C-based `let` destructuring approach (superseded by macro approach on `main`)



## NVlass TODO

### Multi arity `keyword` — DONE 2026-10-10
`(keyword ns name)`, `(symbol ns name)`, `namespace`; qualified symbols and
keywords print with their namespace; `(keyword "a/b")` interns the same
object as the literal `:a/b`.
