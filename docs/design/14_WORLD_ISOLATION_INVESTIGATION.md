# World Isolation Investigation

Status: **investigation / discussion — no decision made yet.**

This is not scoped to any one example. It's a general question about what
beerlang's concurrency architecture should be, long-term, as a language —
not just "how do we make one server example fast."

## Context

While building `examples/embedded_http` (a C host embedding beerlang behind
a fast HTTP library), the natural question came up: can we run one beerlang
VM per OS thread to scale across cores? Investigating that surfaced a fact
about the *current* runtime that matters far beyond that one example: the
entire beerlang runtime — heap, namespaces, symbol table, scheduler — is a
**process-wide singleton**, not an instance you can have more than one
safely-isolated copy of per process. That's fine for a single-threaded
embedding, but it's a real fork in the road for where the language goes
next, because the two ways to fix it lead to genuinely different
concurrency identities for beerlang: isolated-workers (Erlang/Node-cluster
flavored) vs. a shared, thread-safe heap (JVM/Clojure flavored). Given the
goal is "a beautiful, useful Clojure variant," this is worth deciding
deliberately rather than drifting into by accident.

## Current state: an accidental singleton, not a designed one

Full inventory of mutable global state in the runtime today:

| State | Location | Per-thread-unsafe? |
|---|---|---|
| Symbol/keyword intern tables | `src/types/symbol.c` | **Yes** — concurrent interning corrupts the hash table |
| `NamespaceRegistry` | `src/runtime/namespace.c` | **Yes** — concurrent def/lookup races |
| `Scheduler` (ready queues etc.) | `src/scheduler/scheduler.c` | **Yes** — not lock-free |
| IO reactor (1 pthread + kqueue/epoll fd) | `src/io/io_reactor.c` | **Yes** — and not cloneable, only recreatable |
| `gensym_counter`, `requiring_depth` | `src/runtime/core.c` | Yes, but trivial (two ints) |
| `load_units`/`asm_units` (retained compiled code, fixed-size arrays, `MAX_LOAD_UNITS=4096`) | `src/runtime/core.c` | Yes |
| `g_stdin/stdout/stderr_stream` | `src/io/stream.c` | Mostly cosmetic — underlying fds are process-wide anyway |
| `g_destructors` (type→fn table) | `src/memory/alloc.c` | No — immutable after init |
| `g_stats` (alloc counters) | `src/memory/alloc.c` | No — bookkeeping only |
| `global_tar_index` | `src/io/tarindex.c` | No — built once from `BEERPATH`, read-only after |

Object reference counting itself (`object_retain`/`object_release`) needs no
change under any path discussed here, **as long as no single `Value` is ever
touched by two threads** — that invariant is exactly what "isolation"
below is about preserving, cheaply.

### This contradicts the project's own original design

`docs/design/08_MULTITASKING.md` — written before implementation — specifies
a real OS-thread pool sharing one scheduler:

```c
// from 08_MULTITASKING.md's Scheduler struct:
pthread_t*     threads;         // OS thread pool
int            n_threads;       // Number of OS threads
pthread_mutex_t lock;           // Protect scheduler state
pthread_cond_t  work_available; // Signal threads when work available
```
with a documented worker-thread loop (`worker_thread()`) locking that
shared scheduler and pulling tasks off shared ready queues. That's a
shared-heap, multi-OS-thread design — much closer to "Path B" below than to
what actually got built. What's running today is simpler: one cooperative
scheduler, driven by one OS thread at a time (`scheduler_run_one_tick`
called inline from the REPL/embedding loop), with blocking native calls
(FFI, `beer.shell/exec`) dispatched to a separate thread pool rather than
the task scheduler itself being multi-threaded. The global singletons
catalogued above are safe *only* because of that simplification. Worth
being explicit: the current state isn't a deliberate "Path A" choice
either — it's what's left over from not yet building the thread-pool
scheduler the original design called for. Both paths below are real
decisions, not a return to some prior design.

## Path A — Shared-nothing isolated Worlds

Bundle the per-thread-unsafe state above into one `BeerWorld` struct; give
`VM`, `Reader`, and `Compiler` a `world` pointer (the first already flows
through nearly every native and the whole bytecode loop; the latter two
don't take a `VM*` today and would need the pointer added explicitly). Every
touch-point becomes `vm->world->ns_registry` instead of a bare global — one
pointer hop, not a new category of cost, and critically **no thread-local
storage anywhere** (TLS access is a real function call on macOS —
`tlv_get_addr` — not a cheap segment-relative load like on Linux, which
would have been a real tax if we'd needed it on the hot retain/release
path; we don't, because the World pointer rides along on structs that
already exist).

**"Clone" is not a flat memory copy.** Most of a World is immutable after
warmup — interned symbols, compiled function bytecode, `core.beer`'s
macros — and can be shared by pointer, reusing the project's existing
**Immortal Function Templates** mechanism (`REFCOUNT_IMMORTAL`; functions
without captures already share one template object forever). What can't be
shared:
- the IO reactor — not cloneable, only recreatable (one `pthread_create` +
  one `kqueue`/`epoll_create`, the same cost paid once today at startup);
- the scheduler — needs a fresh, empty instance (cheap, just queues);
- genuinely mutable top-level state (atoms, defs holding mutable
  aggregates) — **must** get independent copies per clone, or cloning has
  recreated the exact race Worlds exist to prevent.

Clone cost therefore scales with how much mutable top-level state an app
actually defines, not with the size of the runtime. For a typical
functional-style beerlang app (little or no top-level mutation), clone is
close to free.

**What this buys, and what it costs:**
- Zero overhead on the hot path (no atomics, no TLS).
- Simple to reason about — each World is fully independent, nothing to
  synchronize.
- Generalizes exactly what `examples/embedded_http_prefork` already gets
  for free via `fork()` + copy-on-write — this path is "do the same thing,
  in-process, with threads instead of processes," worth it only if N OS
  processes' memory/startup cost is actually a problem `fork()` doesn't
  already solve.
- **The real cost is philosophical, not just technical**: Clojure's
  signature concurrency primitives — refs, STM, agents, an atom visible
  from two threads — have no referent across Worlds, because nothing is
  shared, by construction. TODO.md's "Long-term / Maybe" list already
  carries `Refs/STM` and `Agents` as aspirations; under Path A those would
  only ever coordinate state *within* one World (i.e. across cooperative
  tasks on one scheduler, which channels already mostly cover), never
  across the OS-thread/process boundary. That's a real narrowing of what
  "beerlang concurrency" means, not a detail to gloss over.

## Path B — Shared heap, thread-safe runtime

The other path: make the single process-wide heap and runtime state
genuinely safe for concurrent OS threads, rather than isolating them. This
is the path `08_MULTITASKING.md` originally sketched.

**What it requires:**
1. **Atomic reference counting.** `obj->refcount` becomes an atomic type;
   retain/release become `LOCK XADD`/`LOCK DEC`-class instructions instead
   of a plain increment — on the order of 10-30x the cost of today's
   operation *before* accounting for real cache-line contention when two
   threads actually touch the same object (which can run to hundreds of
   cycles per bounce). This lands on the single hottest operation class in
   the entire runtime (every stack push/pop of a heap value, every
   collection op) and cuts directly against the project's own stated
   design philosophy — "cache-fit VM," reference counting chosen
   specifically because it's cheap and simple for this design.
2. **Thread-safe symbol/namespace tables.** Either a global lock around
   intern/def/lookup (simple, but a serialization point — though steady-
   state execution of already-compiled code rarely hits these, so
   contention would mostly bite compile-heavy or `eval`/REPL-heavy
   concurrent workloads) or genuine lock-free/concurrent hash tables
   (significantly more correctness risk — this is a notoriously
   hard-to-get-right, hard-to-test class of bug).
3. **GC/cycle-detection redesign.** A future mark-sweep cycle-collector
   pass (already an open TODO item independent of this investigation) needs
   either stop-the-world synchronization across all mutator threads (the
   standard pragmatic answer, same as most production GCs) or a
   meaningfully more sophisticated concurrent collector.
4. **Scheduler becomes the thread pool `08_MULTITASKING.md` already
   describes** — shared ready queues behind a lock/condvar, OS threads
   pulling work, rather than one scheduler per isolated World.

**What this buys, and what it costs:**
- **Actual Clojure semantics.** Persistent data structures are already
  safe for concurrent *reads* by construction (that's the whole point of
  structural sharing) — a shared heap lets that property pay off across
  real OS threads, not just across cooperative tasks on one scheduler.
  Atoms/refs/agents work exactly like real Clojure, no World/clone ceremony
  needed, and existing roadmap items (`Refs/STM`, `Agents`) become
  meaningful rather than needing reinterpretation.
- One heap is more memory-efficient than N duplicated symbol
  tables/namespaces per worker.
- Costs: pervasive atomic overhead on the hottest path in the runtime, and
  substantially higher engineering/correctness risk — concurrent data
  structures are a genuinely hard class of problem, much harder to test
  exhaustively than "everything is isolated, nothing can race."

## Path C — Opt-in shared objects (hybrid, worth prototyping first)

Default to Path A's cheap per-thread isolation, but add an explicit,
opt-in mechanism for values that *should* cross thread boundaries —
conceptually Rust's `Arc<T>` vs. `Rc<T>` split. A new shared-object type
(e.g. a `SharedAtom`, atomically refcounted and living outside any one
World) would be the only thing paying Path B's atomic tax; ordinary values
stay on today's cheap non-atomic path.

This avoids an all-or-nothing choice: the common case (isolated worker
deployments, like `embedded_http_prefork`) stays as cheap as Path A, while
`Refs`/`STM`/`Agents` get a real, working answer for programs that
explicitly want cross-thread shared state — without taxing everyone else
for it. Flagged here as the most promising direction to prototype before
committing to a full Path A or Path B rewrite, since it's additive rather
than a rewrite of the existing single-threaded runtime.

## Comparison

| | Path A: isolated Worlds | Path B: shared heap | Path C: opt-in shared |
|---|---|---|---|
| Hot-path overhead | None | Atomics everywhere | Atomics only on opted-in values |
| Engineering risk | Moderate, bounded (symbol.c, namespace.c, scheduler.c, io_reactor.c) | High (concurrent data structures, GC redesign) | Moderate (new type + Path A's isolation) |
| Matches original `08_MULTITASKING.md` design | No | Yes | Partially |
| Refs/STM/Agents meaningful across OS threads | No (only within one World) | Yes | Yes, for opted-in values |
| Memory per worker | N× (duplicated symbol/ns tables, mitigated by immortal-template sharing) | 1× | ~1× for shared state, N× for isolated state |
| What it generalizes | `examples/embedded_http_prefork`'s process-per-worker, in-process | A real M:N scheduler (as originally designed) | Both, selectively |

## Open questions

- Does the project want beerlang's concurrency identity to be
  Erlang/Node-cluster-style isolated workers, or JVM/Clojure-style shared
  heap with safe concurrent structures? This is a product-philosophy
  decision as much as an engineering one, and it's upstream of picking a
  path.
- Path B or C both depend on the still-open cycle-detection/GC design
  (`05_GARBAGE_COLLECTION.md`) — not independent decisions.
- Separately from whichever path gets chosen: `08_MULTITASKING.md` should
  eventually be reconciled with what's actually implemented today (single-
  threaded cooperative, not the pthread-pool design it currently
  describes) — a smaller, standalone documentation task.

## References

- `examples/embedded_http/README.md` — where this investigation started;
  the process-per-worker prefork plan is the pragmatic near-term answer
  regardless of which path (if any) gets pursued here.
- `docs/design/08_MULTITASKING.md` — original scheduler design (pthread
  pool); contrast with the current single-threaded-cooperative
  implementation.
- `docs/design/MEMORY_MODEL.md`, `docs/design/05_GARBAGE_COLLECTION.md` —
  the refcounting rules this investigation builds on.
- `TODO.md`, "Long-term / Maybe" — `Refs/STM`, `Agents` only fully make
  sense under Path B or C.
