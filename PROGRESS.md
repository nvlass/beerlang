# Beerlang Implementation Progress

**Last Updated:** 2026-04-03

## Current Status

### All Phases Complete — Feature-Rich Language Runtime

Beerlang is a fully functional Clojure-dialect with a stack-based VM, cooperative multitasking, async I/O, actor system, and comprehensive standard library. The language supports interactive REPL-driven development, project tooling, and library distribution.

**Test suite: all passing (100% pass rate)**
- 61 unit tests
- 397 REPL smoke tests

**Milestones achieved:**
- Phase 1: Foundation (types, memory, VM core)
- Phase 2: Collections (cons, vector, hashmap)
- Phase 3: Functions & Calls (closures, tail calls)
- Phase 4: Reader (S-expressions, buffered I/O)
- Phase 5: Compiler (all core special forms)
- Phase 6: REPL v1 (native functions, interactive development)
- Phase 7: Macro system, stdlib, exception handling
- Phase 8: I/O Phase 1 (streams, file I/O, string functions)
- Phase 9: Namespace system (`beer.core`, `ns`, `require`, `:as` aliases)
- Phase 10: Cooperative multitasking (tasks, scheduler, channels), `finally` in try/catch
- Phase 11: Float (double) type — immediate values, mixed arithmetic
- Phase 12: Callable non-functions (IFn-like dispatch), stdlib additions
- Phase 13: Async I/O reactor (kqueue/epoll), non-blocking streams
- Phase 14: TCP sockets, `beer.tcp` namespace
- Phase 15: JSON parser/emitter, HTTP server library
- Phase 16: Project tooling (`beer` CLI, `beer.test`, `beer.shell`)
- Phase 17: Tar-based library distribution, `BEERPATH`
- Phase 18: Bytecode metaprogramming (`asm`/`disasm`)
- Phase 19: `eval`, `keyword`/`name` natives, REPL refactor
- Phase 20: Immortal function templates (refcount safety)
- Phase 21: `task-watch` — task completion monitoring
- Phase 22: `beer.hive` — local actor system with mailbox-based messaging
- Phase 23: Atoms — mutable reference type with CAS
- Phase 24: Docstrings & metadata for vars and functions

## Completed Components

### Core Infrastructure
- **Tagged Union Values**: 16-byte tagged union struct (TAG_NIL, TAG_TRUE, TAG_FALSE, TAG_FIXNUM, TAG_FLOAT, TAG_CHAR, TAG_OBJECT)
- **Memory Management**: Reference counting with type-specific destructors, immortal objects
- **Object System**: Unified header (type, refcount, size, meta)
- **Leak Debugging**: `--dump-leaks` flag via `make track-leaks` build (`BEER_TRACK_ALLOCS`)

### Types
- **Fixnum**: 64-bit signed immediate values
- **Float**: 64-bit double immediate values (TAG_FLOAT), `%g` formatting
- **Bigint**: Arbitrary precision via mini-GMP, auto-promotion from fixnum
- **String**: Immutable UTF-8 with FNV-1a hash caching
- **Symbol/Keyword**: Interned with O(1) equality
- **Cons/List**: Traditional pairs with rich API (map, filter, fold)
- **Vector**: Dynamic arrays with random access
- **HashMap**: HAMT (Hash Array Mapped Trie) with structural sharing
- **Atom**: Mutable reference type with `swap!`, `reset!`, `compare-and-set!`

### Reader
- All Clojure literal types (numbers, strings, symbols, keywords, collections)
- Buffered file input (4KB chunks)
- Quote, quasiquote, unquote, unquote-splicing, deref (`@`), var-quote (`#'`)
- Anonymous fn reader macro (`#(+ % %2)`)
- Line/column error reporting

### Compiler
- **Special forms**: `quote`, `if`, `do`, `def`, `fn`, `let*`, `loop`, `recur`, `try`/`catch`/`finally`, `throw`, `spawn`, `yield`, `await`, `defmacro`
- **Closures**: Free variable detection, `OP_MAKE_CLOSURE`, nested capture propagation
- **Tail calls**: Detected at compile time, emits `OP_TAIL_CALL`
- **Loop/recur**: Compile-time tail position + arity validation
- **ENTER patching**: Correct local allocation even with inner `let` bindings
- **Defmacro docstrings**: Optional string after name, emits `alter-meta!` bytecode

### Virtual Machine
- Stack-based bytecode interpreter (30+ opcodes)
- Call frames with context switching (caller code/constants saved/restored)
- Self-contained function objects (carry their own bytecode/constants pointers)
- Automatic bigint promotion on overflow
- Callable non-functions: keywords, maps, vectors in head position (IFn-like)
- Immortal function templates (`REFCOUNT_IMMORTAL`) for shared constants

### REPL & Runtime
- Read-Eval-Print loop with compilation
- Namespace/Var system for global bindings
- Script mode (`beer file.beer`) and REPL mode
- **Native functions**:
  - Arithmetic: `+`, `-`, `*`, `/` (all variadic, float-aware), `mod`, `rem`, `quot`
  - Comparison: `=`, `<`, `>`, `<=`, `>=` (all variadic, pairwise, mixed numeric types)
  - Collections: `list`, `vector`, `hash-map`, `cons`, `first`, `rest`, `nth`, `count`, `conj`, `empty?`, `get`, `assoc`, `dissoc`, `keys`, `vals`, `contains?`, `concat`, `reduce-kv`
  - Type predicates: `nil?`, `number?`, `string?`, `symbol?`, `keyword?`, `list?`, `vector?`, `map?`, `fn?`, `float?`, `int?`, `char?`, `atom?`
  - Type coercion: `float`, `int`, `keyword`, `name`, `symbol`
  - Utility: `not`, `str`, `type`, `apply`, `gensym`, `eval`, `read-string`, `pr-str`, `prn`
  - I/O: `println`, `print`, `open`, `close`, `read-line`, `read-bytes`, `write`, `flush`, `slurp`, `spit`
  - Concurrency: `chan`, `>!`, `<!`, `close!`, `task?`, `channel?`, `task-watch`
  - Atoms: `atom`, `deref`, `reset!`, `swap!`, `compare-and-set!`
  - Metadata: `meta`, `with-meta`, `alter-meta!`
  - Bytecode: `disasm`, `asm`
  - Namespace: `in-ns`, `require`, `load`, `set-macro!`, `macroexpand-1`, `macroexpand`, `ns-publics`

### Cooperative Multitasking
- Task (TYPE_TASK), Channel (TYPE_CHANNEL) heap types
- Scheduler with instruction-countdown auto-yield (quota=1000)
- `spawn`/`yield`/`await` special forms + VM opcodes
- Buffered and unbuffered (rendezvous) channels
- REPL scheduler drain after each expression
- `task-watch` for monitoring task completion with callbacks

### Async I/O
- Reactor thread with kqueue (macOS) / epoll (Linux)
- Non-blocking streams with `O_NONBLOCK` on file fds
- `native_blocked` flag with `OP_CALL`/`OP_TAIL_CALL` retry logic
- Tasks block on I/O and wake when data is available
- Guard against concurrent stream access from multiple tasks
- Standalone VMs (no scheduler) fall back to blocking I/O

### Networking
- TCP sockets: `tcp/listen`, `tcp/accept`, `tcp/connect`, `tcp/local-port`
- `beer.tcp` wrapper library with high-level API
- `beer.http` — Ring-inspired HTTP server library with middleware
- `beer.json` — pure beerlang JSON parser/emitter

### Project Tooling
- `beer` CLI with subcommands: `new`, `run`, `build`, `ubertar`, `repl`
- `beer.edn` project configuration
- Tar-based library distribution with `BEERPATH` environment variable
- `beer.tar` namespace for tar manipulation
- `beer.shell/exec` — fork/exec/pipe with `{:exit :out :err}` result
- `beer.test` framework: `deftest`, `is`, `testing`, `run-tests`

### Actor System (beer.hive)
- Local actors: task + mailbox (channel), `send`/`receive`
- `hive/spawn-actor`, `hive/send`, `hive/receive`
- `hive/ask` / `hive/reply` — request-reply pattern with envelope wrapping
- `hive/register` / `hive/whereis` — actor name registry
- `hive/supervisor` — supervisor trees with `:one-for-one` strategy

### Metadata & Docstrings
- `meta`, `with-meta`, `alter-meta!` natives for var/function metadata
- `defn` and `defmacro` support optional docstrings
- `doc` macro for printing documentation
- `__print-doc` native for formatted doc output

### Standard Library (lib/core.beer)
- **Macros**: `defn`, `doc`, `when`, `and`, `or`, `cond`, `->`, `->>`, `let`, `with-open`, `doseq`, `ns`, `deftest`, `is`, `testing`
- **Sequence**: `map`, `filter`, `reduce`, `some`, `every?`, `not-any?`, `not-every?`, `take`, `drop`, `partition`, `interleave`, `into`, `frequencies`, `group-by`, `flatten`, `distinct`, `second`, `last`, `butlast`, `range`, `repeat`, `repeatedly`
- **Function combinators**: `comp`, `partial`, `juxt`, `identity`, `constantly`, `complement`
- **Numeric**: `inc`, `dec`, `zero?`, `pos?`, `neg?`, `even?`, `odd?`, `not=`, `max`, `min`, `abs`
- **Map**: `assoc-in`, `get-in`, `update`, `update-in`, `merge`, `select-keys`
- **Sorting**: `sort`, `sort-by` (merge sort)
- **Exception**: `ex-info`
- **Multi-arity dispatch**: `defn` supports multiple arities with `cond` dispatch on `(count args)`

## Key Design Decisions

1. **Reference Counting** over tracing GC — deterministic, simple
2. **Immediate Values** — fixnums, floats, chars, nil/true/false don't allocate
3. **Compile Everything** — REPL expressions compiled to bytecode, no interpreter
4. **Self-contained Functions** — carry code/constants pointers for cross-unit calls
5. **Tail Call Optimization** — built into VM, detected at compile time
6. **Immortal Templates** — `REFCOUNT_IMMORTAL` for shared function constants
7. **Cooperative Multitasking** — scheduler with instruction-countdown auto-yield
8. **HAMT** — persistent hash maps with structural sharing (replaced open-addressing)

## Known Issues

- Reader parses `/` as namespace separator (`(/ x 2)` can fail in nested parens)
- Reader parses `-` as negative number prefix in some contexts
- REPL shutdown leak warnings are expected (kept CompiledCode alive)

## Build Commands

```bash
make              # Standard build
make debug        # Debug build (-g -O0)
make asan         # Address sanitizer build
make track-leaks  # Build with allocation tracking (--dump-leaks)
make test         # Run all unit + smoke tests
make clean        # Remove build artifacts
```

## Source Structure

```
beerlang/
├── include/           # Headers
│   ├── beerlang.h     # Version and master include
│   ├── value.h        # Tagged union definitions + Object header
│   ├── memory.h       # Allocator and refcounting API
│   ├── compiler.h     # Compiler structs (LexicalEnv, CaptureList, RecurTarget)
│   ├── vm.h           # VM, CallFrame, opcodes
│   ├── function.h     # Function/closure with code/constants/meta
│   ├── namespace.h    # Var, Namespace, NamespaceRegistry
│   └── ...            # bigint, bstring, symbol, cons, vector, hashmap, atom, etc.
├── src/
│   ├── compiler/      # compiler.c — all special forms
│   ├── vm/            # vm.c, value.c, fixnum.c, disasm.c
│   ├── memory/        # alloc.c — refcounting + leak tracking
│   ├── reader/        # reader.c, buffer.c
│   ├── runtime/       # core.c (native fns), namespace.c, tcp.c, shell.c
│   ├── types/         # bigint, string, symbol, cons, vector, hashmap, function, native, atom
│   ├── task/          # task.c
│   ├── scheduler/     # scheduler.c
│   ├── channel/       # channel.c
│   ├── io/            # stream.c, reactor.c, io_reactor.c, tarindex.c
│   ├── repl/          # main.c — REPL + CLI entry point
│   └── lib/           # mini-gmp.c, ulog.c
├── lib/
│   ├── core.beer      # Core macros (defn, when, and, or, cond, ->, let, etc.)
│   └── beer/          # Standard libraries
│       ├── json.beer   # JSON parser/emitter
│       ├── http.beer   # Ring-inspired HTTP server
│       ├── tcp.beer    # TCP socket wrapper
│       ├── shell.beer  # Shell execution wrapper
│       ├── test.beer   # Test framework
│       ├── hive.beer   # Actor system
│       └── tools.beer  # Build tooling
├── tests/
│   ├── smoke_test.sh  # 397 end-to-end REPL tests
│   └── ...            # Unit test directories (vm, compiler, types, etc.)
├── docs/
│   ├── API.md         # API reference
│   ├── QUICKSTART.md  # Getting started guide
│   └── design/        # Design documents (01-13)
└── examples/          # Example programs
```

## Next Steps

See TODO.md for the prioritized task list. Key remaining work:
1. **`beer.hive` Phase 2** — distributed actors (node-to-node TCP)
2. **CFFI** — C foreign function interface via libffi
3. **Embeddable library** — `libbeerlang` with C API
4. **AOT compilation** — serialize bytecode to skip parse+compile at runtime
