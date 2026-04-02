# Beerlang Language Design

**A hops-inspired Clojure flavour**

> **📝 Documentation Practice:** This is the main design document. For implementation progress, see:
> - **PROGRESS.md** - Current status, completed features, test results (update after major features)
> - **TODO.md** - Task list and next steps (update when tasks complete)
> - **prompt_history.md** - Session transcripts (update AFTER EVERY PROMPT!)
> - See `.update_checklist.md` for the full update checklist

## Core Philosophy

Beerlang is a LISP-family language that combines:
- Clojure's elegant syntax and data-oriented programming model
- A cache-efficient virtual machine architecture
- Cooperative multitasking for efficient concurrency
- REPL-driven interactive development

## Language Overview

### Syntax and Paradigm

- **Syntax**: Identical to Clojure (LISP family with Clojure's simplifications)
- **Core model**: Minimal special forms + macros building everything else
- **Development**: REPL-based interactive development
- **Namespaces**: Collections of symbols resolving to vars or functions (Clojure-style)

### Compilation and Execution

- **Compilation**: On-the-fly compilation to bytecode (JIT planned for later)
- **REPL Model**: Read → Compile → Execute → Print (no separate interpreter)
  - All code, including REPL expressions, is compiled to bytecode before execution
  - Function definitions are compiled when entered at the REPL
  - Simple expressions are compiled into temporary code segments
  - Ensures consistency and performance across REPL and file-based code
- **VM Architecture**: Stack-based virtual machine
- **Cache Efficiency**: VM designed to fit in L2/L3 cache (possibly with the program itself)
- **Bytecode**: Custom bytecode format executed by the VM

### Concurrency Model

- **Cooperative Multitasking**: All "basic blocks" of code can yield and resume
- **Execution**: Basic blocks are executed by actual CPU threads
- **FFI**: Can load and call shared libraries (dispatched on separate threads)

### Data Types and Structures

Following Clojure's data model as closely as possible:

- **Numbers**: Arbitrary precision arithmetic (using GMP library - Bruno Levy's port on GitHub)
- **Core Collections**:
  - Lists (sequential)
  - Vectors (sequential, indexed)
  - Hash maps (associative)
- **Type System**: Follow Clojure's type model

### Special Forms

Special forms are the minimal set of primitives that cannot be implemented as functions or macros. They control evaluation, affect lexical scope, or provide fundamental operations.

#### Core Special Forms

1. **`def`** - Define a var in the current namespace
   ```clojure
   (def x 42)
   (def my-fn (fn [a] (+ a 1)))
   ```

2. **`if`** - Conditional evaluation (only evaluates chosen branch)
   ```clojure
   (if test then-expr else-expr)
   ```

3. **`do`** - Sequential evaluation, returns last expression
   ```clojure
   (do expr1 expr2 expr3)
   ```

4. **`let`** - Lexical bindings
   ```clojure
   (let [x 10
         y 20]
     (+ x y))
   ```

5. **`quote`** - Prevent evaluation
   ```clojure
   (quote (1 2 3))  ; or '(1 2 3)
   ```

6. **`fn`** - Create anonymous function/closure
   ```clojure
   (fn [x y] (+ x y))
   (fn factorial [n] (if (< n 2) 1 (* n (factorial (- n 1)))))
   ```

7. **`loop`/`recur`** - Structured iteration (optional, can use named fn)
   ```clojure
   (loop [i 0 acc 1]
     (if (< i 10)
       (recur (+ i 1) (* acc 2))
       acc))
   ```
   Note: With implicit tail calls, `loop`/`recur` is syntactic sugar. You can achieve the same with a named function:
   ```clojure
   ((fn loop-fn [i acc]
      (if (< i 10)
        (loop-fn (+ i 1) (* acc 2))  ; Automatically tail-optimized
        acc))
    0 1)
   ```

8. **`try`/`catch`/`finally`** - Exception handling
   ```clojure
   (try
     (risky-operation)
     (catch Exception e
       (handle-error e))
     (finally
       (cleanup)))
   ```

9. **`throw`** - Raise an exception
   ```clojure
   (throw (Exception. "Error message"))
   ```

10. **`yield`** - Cooperative yielding (unique to beerlang!)
    ```clojure
    (yield)  ; Allow other tasks to run
    ```

11. **`spawn`** - Create a new task (green thread)
    ```clojure
    (spawn (fn [] (do-work)))
    ```

12. **`await`** - Wait for a task to complete and get its result
    ```clojure
    (await (spawn (fn [] 42)))  ;=> 42
    ```

13. **`defmacro`** - Define a macro (compile-time expansion)
    ```clojure
    (defmacro my-when "optional docstring" [test & body]
      (list 'if test (cons 'do body) nil))
    ```

> **Note:** `var` (special form #11 in original design) is not yet implemented. Instead, metadata functions (`meta`, `alter-meta!`) accept quoted symbols and resolve them to Vars internally. `disasm` and `asm` are implemented as native functions rather than special forms. See the Bytecode Metaprogramming section for details.

#### Everything Else is Macros

Forms like `defn` (with optional docstrings), `doc`, `when`, `cond`, `and`, `or`, `->`, `->>`, `let`, `with-open`, `doseq`, `ns`, etc. are all implemented as macros on top of these special forms.

### Reader

The reader transforms text into data structures (S-expressions). It follows Clojure's reader syntax closely.

#### Reader Info

See `docs/design/01_READER_DETAILS.md` for more info.

#### Reader Features

**Supported syntax:**
- Lists: `(1 2 3)`
- Vectors: `[1 2 3]`
- Maps: `{:a 1 :b 2}`
- Sets: `#{1 2 3}`
- Strings: `"hello\nworld"`
- Numbers: `42`, `3.14`, `1/2`, `0xFF`, `1e10`
- Characters: `\a`, `\newline`, `\u03BB`
- Symbols: `foo`, `bar/baz`
- Keywords: `:foo`, `:bar/baz`, `::local`
- Special values: `nil`, `true`, `false`
- Comments: `; line comment`
- Quote: `'form` → `(quote form)`
- Deref: `@form` → `(deref form)`
- Metadata: `^{:doc "..."} form`
- Var quote: `#'foo` → `(var foo)`
- Anonymous fn: `#(+ % %2)`
- Discard: `#_ignored`
- Regex: `#"pattern"` (optional)

**Not supported initially:**
- Reader conditionals (`#?`, `#?@`) - for future
- Tagged literals beyond built-ins - for future
- Namespaced maps (`#:ns{:a 1}`) - for future


### Printer

The printer transforms data structures into text representation. It provides both human-readable and machine-readable output, with support for all beerlang data types.

#### Printer Info

See `docs/design/02_PRINTER_DETAILS.md` for more info on the printer, cycle detection and the StringBuilder used.

#### Printer Modes

**Readable mode** (`pr-str`, `prn`):
- Strings printed with quotes and escapes
- Characters printed as `\a`, `\newline`, etc.
- Can be read back with reader
- Used for serialization

**Display mode** (`print-str`, `println`):
- Strings printed without quotes
- Characters printed as-is
- More human-friendly
- Used for output

**Pretty mode** (`pprint`):
- Adds indentation and line breaks
- Makes nested structures easier to read
- Still readable (can be read back)

#### Examples

```clojure
;; Readable printing
(pr-str "hello\nworld")      ; => "\"hello\\nworld\""
(pr-str 'foo/bar)            ; => "foo/bar"
(pr-str [1 2 3])             ; => "[1 2 3]"
(pr-str {:a 1 :b 2})         ; => "{:a 1 :b 2}"
(pr-str #'user/foo)          ; => "#'user/foo"
(pr-str \newline)            ; => "\\newline"

;; Display printing
(println "hello\nworld")     ; prints: hello
                             ;         world
(println [1 2 3])            ; prints: [1 2 3]

;; Pretty printing
(pprint {:users [{:name "Alice" :age 30}
                 {:name "Bob" :age 25}]})
; prints:
; {:users
;   [{:name "Alice"
;     :age 30}
;    {:name "Bob"
;     :age 25}]}
```


### Bytecode Instruction Set

The stack-based VM uses a compact instruction set designed to fit in cache. Instructions are single-byte opcodes with optional operands.

The instruction set is documented in `docs/design/03_INSTRUCTION_SET.md`.

### Bytecode Metaprogramming

Beerlang embraces "code as data" at the bytecode level through the `disasm` and `asm` special forms. This enables powerful metaprogramming, debugging, optimization, and experimentation workflows.

The bytecode metaprogramming philosophy, rationaly, implementation and possible applications are documented in `docs/design/04_BYTECODE_METAPROGRAMMING.md`.

### Memory Management

Beerlang uses **reference counting** as its primary garbage collection strategy. This approach is particularly well-suited to the language's design:

More information and details on memory management and garbage collection approach can be found in `docs/design/05_GARBAGE_COLLECTION.md`.

### Object Representation

Beerlang's object representation is designed for cache efficiency, minimal memory overhead, and efficient reference counting. The design uses **tagged pointers** for immediate values and **headers** for heap-allocated objects.

Object Representation, Data structures and Memory Layout are documented in `docs/design/06_OBJECT_REPRESENTATION.md`.

### Calling Convention

Beerlang uses a stack-based calling convention optimized for closures, tail calls, cooperative yielding, and reference counting.

Calling conventions are documented in `docs/design/07_CALL_CONVENTIONS_AND_FFI.md`.

### Cooperative Multitasking and Scheduler

Beerlang implements cooperative multitasking where lightweight tasks (green threads) are scheduled on a pool of OS threads. Tasks explicitly yield control or are automatically preempted at safe points.

Multitasking and task communication are documented in `docs/design/08_MULTITASKING.md`.

## Design Decisions

### Why Clojure Syntax?

- Proven, minimal, and elegant
- Great for macros and metaprogramming
- Minimal parser complexity

### Why Stack-based VM?

- Simple instruction set
- Cache-friendly when kept small
- Well-understood execution model

### Why Cooperative Multitasking?

- Predictable concurrency without preemption overhead
- Natural fit for VM-based execution
- Allows efficient scheduling of many lightweight tasks

### Why Cache-fit VM?

- Modern performance is dominated by memory access patterns
- L2/L3 cache residency provides dramatic speedup
- Small VM footprint enables this optimization

### Why Compile Everything (Including REPL)?

- Consistency: Same execution path for all code
- Performance: REPL code runs at full VM speed
- Simplicity: No separate interpreter to maintain
- Natural tail calls: Implicit optimization works everywhere

## Compiler

The compiler transforms S-expressions (produced by the reader) into bytecode. It handles macro expansion, special forms, lexical environments, closures, and optimizations like tail call detection.

The compiler structure and implementation details are documented in `docs/design/09_COMPILER.md`.

## REPL Evaluation Model

Beerlang's REPL follows a **compile-everything** approach: all code, including simple expressions typed at the REPL, is compiled to bytecode before execution.

The REPL Execution flow is documented in `docs/design/10_REPL.md`.

### Does This Affect Design Principles?

**No - it reinforces them:**

- ✅ **Cache-efficient**: Bytecode is compact, fits in cache
- ✅ **Cooperative multitasking**: Works identically in REPL
- ✅ **Tail calls**: Implicit optimization during compilation
- ✅ **Reference counting**: Same GC in REPL and compiled code
- ✅ **Test-driven**: REPL code can be tested like any code
- ✅ **Simpler**: No interpreter → smaller VM → more cache-friendly

The compile-everything approach actually makes the system **simpler and more consistent** rather than more complex. It's one unified path from source to execution.

## Input/Output System

Beerlang's I/O system is designed to work seamlessly with cooperative multitasking. All I/O operations are non-blocking at the task level, allowing tasks to perform I/O without blocking other tasks.

The IO philosophy and implementation info are documented in `docs/design/11_IO.md`.

### Performance Considerations

1. **Buffering**: Default 8KB buffers reduce syscalls
2. **Batch writes**: Accumulate small writes
3. **Zero-copy**: Use `sendfile` for large transfers when possible
4. **Read-ahead**: Predict sequential reads
5. **Memory-mapped files**: For large file access

### Integration with Scheduler

I/O operations integrate seamlessly with the task scheduler:

```
Task calls read() → Buffer empty → Initiate async read → Block task
                                                           ↓
I/O Scheduler: epoll_wait() → Data ready → Wake task → Resume execution
```

### Future Extensions

1. **HTTP client/server** built on streams
2. **WebSocket** support
3. **TLS/SSL** streams
4. **Compression** streams (gzip, etc.)
5. **Async file system operations** (list dir, stat, etc.)
6. **Memory-mapped I/O**
7. **Direct I/O** (bypass cache)
8. **io_uring** support on Linux for even better performance

## Development Methodology

### Test-Driven Development

Beerlang follows a strict **test-first** approach where tests are written before implementation. This is particularly valuable for language and VM development.

#### Why TDD for a Programming Language?

1. **Tests define semantics** - They specify exact behavior of language constructs
2. **Early API design** - Writing tests first exposes interface problems
3. **Comprehensive edge cases** - Forces thinking about corner cases upfront
4. **Fearless refactoring** - Change internals without breaking behavior
5. **Living documentation** - Tests show how features should work
6. **Regression prevention** - Critical as VM and compiler evolve

#### Test Categories

**VM/Bytecode Tests:**
- Unit tests for each bytecode instruction
- Stack manipulation tests (push, pop, overflow, underflow)
- Memory management tests (refcounting, deferred deletion, cycles)
- Cooperative yielding and scheduling tests
- Exception handling and unwinding tests
- Instruction sequence integration tests
- Performance tests (cache residency, throughput)

**Compiler Tests:**
- Special form compilation (source → bytecode)
- Macro expansion correctness
- Optimization tests (tail call detection, constant folding)
- Closure capture and environment building
- Error reporting and source location tracking
- Edge cases (deeply nested forms, mutual recursion)

**Language/Runtime Tests:**
- Standard library function tests
- Data structure tests (persistent operations, structural sharing)
- REPL behavior and interaction tests
- Namespace and var resolution tests
- FFI and native library interaction
- Complete program integration tests

**Property-Based Tests:**
- Refcounting correctness (no leaks, no double-frees)
- Persistent data structure properties (immutability, efficiency)
- Arithmetic correctness (against reference implementations)
- Bytecode generation invariants

#### Test Organization

```
tests/
├── vm/
│   ├── instructions/     # Per-instruction unit tests
│   ├── stack/            # Stack operation tests
│   ├── memory/           # GC and refcounting tests
│   └── integration/      # Multi-instruction scenarios
├── compiler/
│   ├── special-forms/    # Compilation of each special form
│   ├── macros/           # Macro expansion tests
│   └── optimization/     # Optimization correctness
├── runtime/
│   ├── core/             # Standard library tests
│   ├── data-structures/  # List, vector, map tests
│   └── ffi/              # FFI integration tests
└── integration/
    ├── programs/         # Complete working programs
    └── benchmarks/       # Performance tests
```

#### TDD Workflow

1. **Write the test** - Define expected behavior
2. **Watch it fail** - Verify test catches the missing feature
3. **Implement minimally** - Just enough to pass the test
4. **Refactor** - Clean up while tests stay green
5. **Repeat** - Next feature

#### Benefits for Beerlang Specifically

- **Cache efficiency claims** - Benchmarks prove VM fits in cache
- **Cooperative yielding** - Tests verify tasks properly yield and resume
- **Refcounting** - Tests catch memory leaks and premature frees
- **Bytecode correctness** - Every instruction is validated
- **Cross-platform** - Same tests run on all target platforms

### Version Control Strategy

- Small, focused commits (one feature/test at a time)
- Tests committed before implementation
- Clear commit messages referencing what test they satisfy
- Feature branches for experimental changes

## Standard Library

The beerlang standard library provides essential functions and utilities for functional programming, data manipulation, I/O, concurrency, and more. It follows Clojure's core library philosophy but with beerlang-specific additions.

The standard library is documented in `docs/design/12_STDLIB.md`

## Open Questions and Future Work

### Near-term (v1.0)

1. **Bytecode format details**: Compact encoding, operand sizes, versioning
2. **Constant pool**: Structure, limits, deduplication strategy
3. **FFI marshalling**: Precise type conversion, struct handling
4. **Cycle detection**: Frequency and strategy for reference counting
5. **Regex backend**: Choose between PCRE, RE2, or POSIX regex

### Medium-term (v2.0)

6. **JIT compilation**: Strategy, tier selection, deoptimization
7. **Advanced tooling**:
   - Debugger with breakpoints and stepping
   - Profiler (sampling and instrumentation)
   - Memory profiler
   - Bytecode optimizer
8. **Performance optimizations**:
   - Inline caching for polymorphic calls
   - Type specialization
   - Escape analysis
   - Loop unrolling

### Long-term / Maybe

9. **Advanced concurrency**:
   - Refs/STM (if proven necessary - high complexity)
   - Agents (if async state updates needed beyond channels)
10. **Protocols**: Protocol-based polymorphism (Clojure-style)
11. **Records/Types**: User-defined types with optimized field access
12. **Multiple regex backends**: Pluggable regex engines
13. **Distributed computing**: Remote tasks, distributed channels
14. **Hot code reloading**: Update running system without restart
15. **Native compilation**: AOT compiler to native code

## Implementation Priorities

### Implementation Language: C

**C is the recommended choice** for beerlang's VM implementation:

**Advantages:**
- Direct memory control (essential for cache-efficient design)
- Zero runtime overhead
- Portable across all platforms
- Simple FFI to libraries (GMP, PCRE, system calls)
- Predictable performance characteristics
- Well-proven for VM implementation (Lua, Python, Ruby all use C)

**Project Structure:**
```
beerlang/
├── src/
│   ├── vm/           # Virtual machine core
│   ├── types/        # Object representations
│   ├── memory/       # Reference counting, allocation
│   ├── reader/       # S-expression reader
│   ├── compiler/     # Bytecode compiler
│   ├── runtime/      # Runtime library
│   ├── scheduler/    # Task scheduler
│   ├── io/           # I/O system
│   └── repl/         # REPL
├── include/          # Public headers
├── tests/            # Test suite
│   ├── vm/
│   ├── compiler/
│   └── runtime/
├── examples/         # Example programs
└── docs/            # Documentation
```

### Phased Implementation Approach

Each phase builds on the previous, allowing incremental testing and validation.

---

### Phase 1: Foundation (Week 1-2)

**Goal:** Basic VM that can execute hand-written bytecode

**Tasks:**
1. **Value representation**
   - Tagged pointers (64-bit)
   - Immediate values (fixnum, char, nil, true, false)
   - Heap object header structure

2. **Memory management basics**
   - Simple allocator
   - Reference counting (inc/dec)
   - No cycle detection yet

3. **Basic types**
   - Fixnum operations
   - Boolean operations
   - Nil

4. **VM core**
   - Value stack
   - Instruction fetch/decode/execute loop
   - Basic instructions: PUSH_INT, ADD, SUB, POP, DUP

5. **Testing**
   - Hand-write bytecode
   - Test arithmetic
   - Test stack operations

**Milestone:** Execute `2 + 3 * 4` from hand-written bytecode

---

### Phase 2: More Types & Instructions (Week 3)

**Goal:** Complete basic type system

**Tasks:**
1. **String type**
   - Heap-allocated strings
   - String equality
   - String printing

2. **Symbol type**
   - Symbol interning
   - Global symbol table

3. **Collections (simple versions)**
   - Cons cells (linked lists)
   - Basic vector (dynamic array)
   - Basic hash map (simple hash table)

4. **More VM instructions**
   - Comparison (LT, GT, EQ, etc.)
   - Logic (NOT, etc.)
   - Stack manipulation (SWAP, OVER)
   - Jump instructions (JUMP, JUMP_IF_FALSE)

5. **Testing**
   - Test each type
   - Test collection operations
   - Test control flow

**Milestone:** Execute conditional logic from hand-written bytecode

---

### Phase 3: Functions & Calls (Week 4)

**Goal:** Function calls and basic closures

**Tasks:**
1. **Function objects**
   - Function type
   - Code pointer
   - Arity

2. **Calling convention**
   - CALL instruction
   - RETURN instruction
   - ENTER instruction
   - Stack frames
   - Local variables (LOAD_LOCAL, STORE_LOCAL)

3. **Simple closures**
   - Capture variables
   - LOAD_CLOSURE instruction
   - MAKE_CLOSURE instruction

4. **Testing**
   - Test function calls
   - Test recursion
   - Test closures

**Milestone:** Execute recursive factorial from hand-written bytecode

---

### Phase 4: Reader (Week 5)

**Goal:** Parse Clojure syntax into data structures

**Tasks:**
1. **Lexer**
   - Tokenize input
   - Handle whitespace and comments

2. **Parser**
   - Parse lists, vectors, maps
   - Parse literals (numbers, strings, symbols, keywords)
   - Parse reader macros (quote, etc.)

3. **Error handling**
   - Line/column tracking
   - Meaningful error messages

4. **Testing**
   - Test parsing of all syntax forms
   - Test error cases

**Milestone:** Parse `(+ 1 2)` into data structures

---

### Phase 5: Simple Compiler (Week 6-7)

**Goal:** Compile S-expressions to bytecode

**Tasks:**
1. **Compilation pipeline**
   - Analyze forms
   - Generate bytecode
   - Constant pool

2. **Compile literals**
   - Numbers, strings, keywords, etc.

3. **Compile function calls**
   - Argument evaluation
   - Function evaluation
   - CALL instruction

4. **Compile special forms (basic)**
   - `if`
   - `do`
   - `def`
   - `fn`

5. **Testing**
   - Test each compilation path
   - Compare generated bytecode

**Milestone:** Compile and execute `(def x (+ 1 2))` `(println x)`

---

### Phase 6: REPL v1 (Week 8)

**Goal:** Working read-eval-print loop

**Tasks:**
1. **REPL loop**
   - Read from stdin
   - Compile
   - Execute
   - Print result

2. **Namespace basics**
   - Current namespace
   - Var storage
   - Var lookup

3. **Core functions (native)**
   - Arithmetic: +, -, *, /
   - Comparison: =, <, >
   - I/O: println
   - Collections: list, vector, first, rest

4. **Testing**
   - Interactive testing
   - REPL sessions

**Milestone:** Interactive REPL that can define functions and execute them

---

### Phase 7: Complete Compiler (Week 9-10)

**Goal:** All special forms and optimizations

**Tasks:**
1. **Remaining special forms**
   - `let`
   - `loop`/`recur`
   - `try`/`catch`/`finally`
   - `throw`
   - `quote`
   - `var`

2. **Lexical environments**
   - Proper scoping
   - Closure capture detection

3. **Tail call optimization**
   - Detect tail position
   - Emit TAIL_CALL
   - Test recursion

4. **Macro system**
   - `defmacro`
   - Macro expansion
   - Compile-time evaluation

5. **Testing**
   - Comprehensive compiler tests
   - Test all special forms

**Milestone:** Compile complex programs with macros and recursion

---

### Phase 8: Concurrency (Week 11-12)

**Goal:** Cooperative multitasking

**Tasks:**
1. **Task structure**
   - Task object
   - Task states
   - Task stack

2. **Scheduler**
   - Ready queues
   - Context switching
   - Worker threads

3. **spawn/await**
   - Create tasks
   - Wait for completion

4. **Channels**
   - Channel object
   - Send/receive
   - Blocking/yielding

5. **Testing**
   - Test task creation
   - Test channel communication
   - Test concurrent programs

**Milestone:** Multiple tasks communicating via channels

---

### Phase 9: I/O System (Week 13-14)

**Goal:** Non-blocking I/O with reactor pattern

**Tasks:**
1. **Stream abstraction**
   - Stream object
   - Buffering

2. **Reactor threads**
   - epoll/kqueue integration
   - Event loop
   - Wake blocked tasks

3. **File I/O**
   - Open, read, write, close
   - Async operations

4. **Standard streams**
   - stdin, stdout, stderr
   - REPL integration

5. **Testing**
   - Test file operations
   - Test concurrent I/O

**Milestone:** Read/write files concurrently from multiple tasks

---

### Phase 10: Standard Library (Week 15-16)

**Goal:** Essential library functions

**Tasks:**
1. **Core functions (pure beerlang)**
   - Sequence operations (map, filter, reduce)
   - Collection operations
   - Higher-order functions

2. **String library**
   - String manipulation
   - Formatting

3. **Math library**
   - Trigonometry
   - Exponential/log
   - Random

4. **Regex (FFI to PCRE)**
   - Pattern compilation
   - Matching
   - Replacement

5. **Testing**
   - Test each library function
   - Integration tests

**Milestone:** Rich standard library for practical programming

---

### Phase 11: Polish & Optimization (Week 17-18)

**Goal:** Production-ready v1.0

**Tasks:**
1. **Memory optimizations**
   - Deferred deletion
   - Cycle detection (simple)

2. **Compiler optimizations**
   - Constant folding
   - Dead code elimination

3. **Error handling**
   - Better error messages
   - Stack traces

4. **Documentation**
   - API documentation
   - Tutorial
   - Examples

5. **Benchmarking**
   - Performance tests
   - Memory profiling
   - Optimization based on results

**Milestone:** Beerlang v1.0 release!

---

### Development Tools & Practices

**Build System:**
```bash
# Makefile targets
make          # Build VM
make test     # Run tests
make repl     # Start REPL
make clean    # Clean build
```

**Testing Strategy:**
- Unit tests for each module (using any C test framework)
- Integration tests for end-to-end functionality
- Hand-written bytecode tests early on
- Beerlang test files once REPL works

**Version Control:**
- Git from day one
- Small, focused commits
- Test before commit
- Document design decisions in commit messages

**Dependencies:**
- GMP (arbitrary precision arithmetic) - Bruno Levy's port
- PCRE or POSIX regex (for regex support)
- Platform-specific: epoll (Linux), kqueue (BSD/macOS)

**Debugging Tools:**
- gdb/lldb for C debugging
- Bytecode disassembler (implement early)
- Trace mode (print executed instructions)
- Memory leak detection (valgrind)

---

### Quick Win Strategy

For **fastest path to a working demo:**

1. **Week 1**: VM core + basic arithmetic
2. **Week 2**: More types + control flow
3. **Week 3**: Function calls
4. **Week 4**: Reader
5. **Week 5**: Simple compiler
6. **Week 6**: REPL

After 6 weeks, you have a **working REPL** that can:
- Define functions
- Execute arithmetic
- Use basic control flow
- Print results

Then iterate to add remaining features!

## Implementation Status

**All phases complete — feature-rich language runtime.** See PROGRESS.md for full details.

**Test suite:** 61 unit tests + 397 REPL smoke tests (100% pass rate)

**What's implemented:**
- All 11 planned phases plus extensive post-plan work
- Stack-based VM with 30+ opcodes, cooperative multitasking, async I/O (kqueue/epoll)
- Full compiler: all special forms, closures, tail calls, macros, defmacro docstrings
- Rich type system: fixnum, float, bigint, string, symbol, keyword, cons, vector, HAMT hashmap, atom
- Namespace system with `require`, aliases, qualified symbols, `beer.core` fallback
- TCP sockets, JSON parser/emitter, HTTP server library
- Actor system (`beer.hive`): spawn, send, receive, ask/reply, supervisors, name registry
- Project tooling: `beer` CLI (new, run, build, ubertar), `beer.test` framework
- Tar-based library distribution with `BEERPATH`
- Bytecode metaprogramming (`asm`/`disasm`), `eval`, `read-string`
- Docstrings and metadata on vars and functions
- Immortal function templates, atoms with CAS, task-watch

**Remaining work (see TODO.md):**
- `beer.hive` Phase 2: distributed actors (node-to-node TCP)
- CFFI: C foreign function interface via libffi
- Embeddable library: `libbeerlang` with C API
- AOT compilation: serialize bytecode to `.beerc` files

## References

- GMP arbitrary precision arithmetic: Bruno Levy's port (GitHub)
- Clojure: https://clojure.org/
- Stack-based VM architectures
