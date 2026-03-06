# Beerlang Implementation Progress

**Last Updated:** 2026-03-06

## Current Status

### Phase 12 Complete: Callable Non-Functions & Stdlib Expansion

Beerlang has a working REPL with closures, loop/recur, macros, exception handling (including `finally`), rich standard library, file I/O, a multi-namespace system with `require` and aliases, cooperative multitasking with tasks, scheduler, and CSP channels, float (double) numeric type, and callable non-functions (keywords, maps, vectors in head position).

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
- Phase 11: Float (double) type — immediate values, mixed arithmetic, division semantics change
- Phase 12: Callable non-functions (IFn-like dispatch), stdlib additions (`comp`, `partial`, `juxt`, `sort`, `sort-by`, `flatten`, `distinct`, `select-keys`, `max`, `min`, `abs`)

**Test suite: all passing (100% pass rate)**
- 61 unit tests
- 303 REPL smoke tests

## Completed Components

### Core Infrastructure
- **Tagged Pointers**: 3-bit tagging (fixnum, char, special, pointer)
- **Memory Management**: Reference counting with type-specific destructors
- **Object System**: Unified header (type, refcount, size, meta)
- **Leak Debugging**: `--dump-leaks` flag via `make track-leaks` build (`BEER_TRACK_ALLOCS`)

### Types
- **Fixnum**: 61-bit signed immediate values
- **Float**: 64-bit double immediate values (TAG_FLOAT), `%g` formatting
- **Bigint**: Arbitrary precision via mini-GMP, auto-promotion from fixnum
- **String**: Immutable UTF-8 with FNV-1a hash caching
- **Symbol/Keyword**: Interned with O(1) equality
- **Cons/List**: Traditional pairs with rich API (map, filter, fold)
- **Vector**: Dynamic arrays with random access
- **HashMap**: Open addressing, linear probing, 75% load factor resize

### Reader
- All Clojure literal types (numbers, strings, symbols, keywords, collections)
- Buffered file input (4KB chunks)
- Quote reader macro (`'x` -> `(quote x)`)
- Line/column error reporting

### Compiler
- **Special forms**: `quote`, `if`, `do`, `def`, `fn`, `let*`, `loop`, `recur`, `try`/`catch`/`finally`, `throw`, `spawn`, `yield`, `await`
- **Closures**: Free variable detection, `OP_MAKE_CLOSURE`, nested capture propagation
- **Tail calls**: Detected at compile time, emits `OP_TAIL_CALL`
- **Loop/recur**: Compile-time tail position + arity validation
- **ENTER patching**: Correct local allocation even with inner `let` bindings

### Virtual Machine
- Stack-based bytecode interpreter (30+ opcodes)
- Call frames with context switching (caller code/constants saved/restored)
- Self-contained function objects (carry their own bytecode/constants pointers)
- Automatic bigint promotion on overflow

### REPL & Runtime
- Read-Eval-Print loop with compilation
- Namespace/Var system for global bindings
- **Native functions**:
  - Arithmetic: `+`, `-`, `*`, `/` (all variadic, float-aware), `mod`, `rem`, `quot`
  - Comparison: `=`, `<`, `>`, `<=`, `>=` (all variadic, pairwise, mixed numeric types)
  - Collections: `list`, `vector`, `hash-map`, `cons`, `first`, `rest`, `nth`, `count`, `conj`, `empty?`, `get`, `assoc`, `dissoc`, `keys`, `vals`, `contains?`
  - Type predicates: `nil?`, `number?`, `string?`, `symbol?`, `keyword?`, `list?`, `vector?`, `map?`, `fn?`, `float?`, `int?`
  - Type coercion: `float`, `int`
  - Utility: `not`, `str`, `type`, `apply`, `println`

## Key Design Decisions

1. **Reference Counting** over tracing GC - deterministic, simple
2. **Immediate Values** - fixnums, floats, chars, nil/true/false don't allocate
3. **Compile Everything** - REPL expressions compiled to bytecode, no interpreter
4. **Self-contained Functions** - carry code/constants pointers for cross-unit calls
5. **Tail Call Optimization** - built into VM, detected at compile time

## Known Issues

- Reader parses `/` as namespace separator (`(/ x 2)` can fail in nested parens)
- Reader parses `-` as negative number prefix in some contexts
- REPL shutdown leak warnings are expected (kept CompiledCode alive)

## Build Commands

```bash
make              # Standard build
make debug        # Debug build (-g -O0)
make track-leaks  # Build with allocation tracking (--dump-leaks)
make test         # Run all unit tests
make repl         # Start REPL
make clean        # Remove build artifacts
```

## Source Structure

```
beerlang/
├── include/           # Headers
│   ├── beerlang.h     # Version and master include
│   ├── value.h        # Tagged pointer definitions + Object header
│   ├── memory.h       # Allocator and refcounting API
│   ├── compiler.h     # Compiler structs (LexicalEnv, CaptureList, RecurTarget)
│   ├── vm.h           # VM, CallFrame, opcodes
│   ├── function.h     # Function/closure with code/constants pointers
│   └── ...            # bigint, bstring, symbol, cons, vector, hashmap, reader, etc.
├── src/
│   ├── compiler/      # compiler.c - all special forms
│   ├── vm/            # vm.c, value.c, fixnum.c, disasm.c
│   ├── memory/        # alloc.c - refcounting + leak tracking
│   ├── reader/        # reader.c, buffer.c
│   ├── runtime/       # core.c (native fns), namespace.c
│   ├── types/         # bigint, string, symbol, cons, vector, hashmap, function, native
│   ├── repl/          # main.c - REPL entry point
│   └── ...
├── tests/
│   ├── compiler/      # test_compiler, test_def, test_fn, test_let
│   ├── runtime/       # test_core (72 integration tests)
│   ├── reader/        # test_reader (91 tests)
│   ├── vm/            # test_vm, test_function, test_fixnum, test_value, test_disasm
│   ├── types/         # test_bigint, test_cons, test_hashmap, test_string, test_symbol, test_vector
│   ├── memory/        # test_alloc
│   ├── task/          # test_task (4 tests)
│   ├── scheduler/     # test_scheduler (4 tests)
│   ├── channel/       # test_channel (3 tests)
│   └── smoke_test.sh  # 303 end-to-end REPL tests
└── docs/              # API reference, quickstart, and design/ subdirectory
```

## Next Steps

1. **I/O Phase 2** — async reactor (kqueue/epoll) integrated with cooperative scheduler
2. **Library distribution** — tar-based bundles with source (.beer files)
3. **Tooling** — CLI tool, rlwrap for line editing, test framework
