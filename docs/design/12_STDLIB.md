### Library Organization

> **Note:** Only `beerlang.core` is currently implemented. All other namespaces are planned for future versions.

```
beerlang.core          ; Core functions (auto-imported)         — IMPLEMENTED
beerlang.string        ; String manipulation                    — NOT YET IMPLEMENTED
beerlang.io            ; I/O operations                         — NOT YET IMPLEMENTED
beerlang.async         ; Concurrency primitives                 — NOT YET IMPLEMENTED
beerlang.math          ; Mathematical functions                 — NOT YET IMPLEMENTED
beerlang.data          ; Data structure operations              — NOT YET IMPLEMENTED
beerlang.regex         ; Regular expressions                    — NOT YET IMPLEMENTED
beerlang.time          ; Time and date                          — NOT YET IMPLEMENTED
beerlang.test          ; Testing framework                      — NOT YET IMPLEMENTED
beerlang.repl          ; REPL utilities                         — NOT YET IMPLEMENTED
beerlang.bytecode      ; Bytecode metaprogramming               — NOT YET IMPLEMENTED
beerlang.system        ; System interface                       — NOT YET IMPLEMENTED
```

### Core Library (beerlang.core)

The core namespace is automatically imported into every namespace. It provides fundamental operations.

#### Special Forms

**Implemented:** `def`, `if`, `do`, `let*`, `quote`, `quasiquote`, `fn`, `loop`, `recur`, `defmacro`, `try`, `catch`, `throw`

**Not yet implemented:** `yield`, `var`, `.` (interop), `disasm`, `asm`

Note: `let` (with destructuring) is a macro in `lib/core.beer` that expands to `let*`.

#### Core Functions - Sequence Operations

**Status: Partially implemented**

```clojure
;; Creation
(list 1 2 3)           ; Create list                    — IMPLEMENTED (native)
(vector 1 2 3)         ; Create vector (also [1 2 3])   — IMPLEMENTED (native)
(hash-map :a 1 :b 2)   ; Create map (also {:a 1 :b 2}) — IMPLEMENTED (native)
(hash-set 1 2 3)       ; Create set (also #{1 2 3})     — IMPLEMENTED (native)
(range n)              ; Sequence 0..n-1                 — IMPLEMENTED (core.beer)
(range start end)      ; Sequence start..end-1           — IMPLEMENTED (core.beer)
(repeat n x)           ; Repeat x n times                — IMPLEMENTED (core.beer)
(repeatedly n f)       ; Call f n times                  — IMPLEMENTED (core.beer)

;; Access
(first coll)           ; First element                   — IMPLEMENTED (native)
(rest coll)            ; Rest of collection (not first)  — IMPLEMENTED (native)
(next coll)            ; Next elements (or nil if empty) — NOT YET IMPLEMENTED
(last coll)            ; Last element                    — IMPLEMENTED (core.beer)
(nth coll i)           ; Element at index i              — IMPLEMENTED (native)
(get map key)          ; Get value by key                — IMPLEMENTED (native)
(get map key default)  ; Get with default                — IMPLEMENTED (native)
(get-in m ks)          ; Nested get                      — IMPLEMENTED (core.beer)

;; Modification (persistent/immutable)
(cons x coll)          ; Add to front                    — IMPLEMENTED (native)
(conj coll x)          ; Add to collection               — IMPLEMENTED (native)
(assoc map k v)        ; Associate key with value        — IMPLEMENTED (native)
(assoc-in m ks v)      ; Nested assoc                    — IMPLEMENTED (core.beer)
(dissoc map k)         ; Remove key                      — IMPLEMENTED (native)
(update map k f)       ; Update value                    — IMPLEMENTED (core.beer)
(update-in m ks f)     ; Nested update                   — IMPLEMENTED (core.beer)

;; Transformation
(map f coll)           ; Map function over collection    — IMPLEMENTED (native)
(mapv f coll)          ; Map, return vector              — NOT YET IMPLEMENTED
(filter pred coll)     ; Keep elements matching pred     — IMPLEMENTED (native)
(remove pred coll)     ; Remove elements matching pred   — NOT YET IMPLEMENTED
(reduce f coll)        ; Reduce with function            — IMPLEMENTED (native)
(reduce f init coll)   ; Reduce with initial value       — IMPLEMENTED (native)
(into to from)         ; Pour from into to               — IMPLEMENTED (core.beer)
(concat & colls)       ; Concatenate collections         — IMPLEMENTED (native)

;; Predicates
(seq coll)             ; Return seq or nil if empty      — NOT YET IMPLEMENTED
(empty? coll)          ; Is collection empty?            — IMPLEMENTED (native)
(not-empty coll)       ; Return coll or nil if empty     — NOT YET IMPLEMENTED
(some pred coll)       ; Return first truthy (pred x)    — IMPLEMENTED (core.beer)
(every? pred coll)     ; True if all match predicate     — IMPLEMENTED (core.beer)
(contains? coll k)     ; Does collection contain key?    — IMPLEMENTED (native)

;; Partitioning
(take n coll)          ; First n elements                — IMPLEMENTED (core.beer)
(drop n coll)          ; All but first n                 — IMPLEMENTED (core.beer)
(take-while pred coll) ; Take while predicate true       — IMPLEMENTED (core.beer)
(drop-while pred coll) ; Drop while predicate true       — IMPLEMENTED (core.beer)
(partition n coll)     ; Partition into n-sized chunks   — IMPLEMENTED (core.beer)
(partition-by f coll)  ; Partition when (f x) changes    — NOT YET IMPLEMENTED
(group-by f coll)      ; Group by result of f            — IMPLEMENTED (core.beer)

;; Sorting
(sort coll)            ; Sort collection                 — NOT YET IMPLEMENTED
(sort-by f coll)       ; Sort by key function            — NOT YET IMPLEMENTED
(reverse coll)         ; Reverse collection              — IMPLEMENTED (core.beer)

;; Set operations
(union s1 s2)          ; Set union                       — NOT YET IMPLEMENTED
(intersection s1 s2)   ; Set intersection                — NOT YET IMPLEMENTED
(difference s1 s2)     ; Set difference                  — NOT YET IMPLEMENTED

;; Counting
(count coll)           ; Number of elements              — IMPLEMENTED (native)
(frequencies coll)     ; Map of value -> count           — IMPLEMENTED (core.beer)
```

#### Core Functions - Predicates

**Status: Mostly implemented**

```clojure
;; Type checking
(nil? x)               ; Is nil?                        — IMPLEMENTED (native)
(some? x)              ; Not nil?                       — NOT YET IMPLEMENTED
(true? x)              ; Is true?                       — NOT YET IMPLEMENTED
(false? x)             ; Is false?                      — NOT YET IMPLEMENTED
(number? x)            ; Is number?                     — IMPLEMENTED (native)
(integer? x)           ; Is integer?                    — NOT YET IMPLEMENTED
(float? x)             ; Is float?                      — NOT YET IMPLEMENTED
(string? x)            ; Is string?                     — IMPLEMENTED (native)
(keyword? x)           ; Is keyword?                    — IMPLEMENTED (native)
(symbol? x)            ; Is symbol?                     — IMPLEMENTED (native)
(list? x)              ; Is list?                       — IMPLEMENTED (native)
(vector? x)            ; Is vector?                     — IMPLEMENTED (native)
(map? x)               ; Is map?                        — IMPLEMENTED (native)
(set? x)               ; Is set?                        — NOT YET IMPLEMENTED
(fn? x)                ; Is function?                   — IMPLEMENTED (native)
(coll? x)              ; Is collection?                 — NOT YET IMPLEMENTED
(seq? x)               ; Is sequence?                   — IMPLEMENTED (native)

;; Comparison
(= x y)                ; Equality                       — IMPLEMENTED (native)
(not= x y)             ; Inequality                     — IMPLEMENTED (core.beer)
(< x y)                ; Less than                      — IMPLEMENTED (native)
(<= x y)               ; Less or equal                  — IMPLEMENTED (native)
(> x y)                ; Greater than                   — IMPLEMENTED (native)
(>= x y)               ; Greater or equal               — IMPLEMENTED (native)
(compare x y)          ; Compare (-1, 0, 1)             — NOT YET IMPLEMENTED
(identical? x y)       ; Reference equality             — NOT YET IMPLEMENTED

;; Logic
(and x y ...)          ; Logical and (macro)            — IMPLEMENTED (core.beer)
(or x y ...)           ; Logical or (macro)             — IMPLEMENTED (core.beer)
(not x)                ; Logical not                    — IMPLEMENTED (native)
```

#### Core Functions - Math

**Status: Partially implemented**

```clojure
;; Arithmetic
(+ x y ...)            ; Addition                       — IMPLEMENTED (native, variadic)
(- x y ...)            ; Subtraction                    — IMPLEMENTED (native, variadic)
(* x y ...)            ; Multiplication                 — IMPLEMENTED (native, variadic)
(/ x y ...)            ; Division                       — IMPLEMENTED (native, variadic)
(quot x y)             ; Quotient                       — NOT YET IMPLEMENTED
(rem x y)              ; Remainder                      — IMPLEMENTED (native)
(mod x y)              ; Modulo                         — IMPLEMENTED (native)
(inc x)                ; Increment                      — IMPLEMENTED (native)
(dec x)                ; Decrement                      — IMPLEMENTED (native)

;; Comparison
(max x y ...)          ; Maximum                        — NOT YET IMPLEMENTED
(min x y ...)          ; Minimum                        — NOT YET IMPLEMENTED
(zero? x)              ; Is zero?                       — IMPLEMENTED (core.beer)
(pos? x)               ; Is positive?                   — IMPLEMENTED (core.beer)
(neg? x)               ; Is negative?                   — IMPLEMENTED (core.beer)
(even? x)              ; Is even?                       — IMPLEMENTED (core.beer)
(odd? x)               ; Is odd?                        — IMPLEMENTED (core.beer)

;; Bitwise
(bit-and x y)          ; Bitwise AND                    — NOT YET IMPLEMENTED
(bit-or x y)           ; Bitwise OR                     — NOT YET IMPLEMENTED
(bit-xor x y)          ; Bitwise XOR                    — NOT YET IMPLEMENTED
(bit-not x)            ; Bitwise NOT                    — NOT YET IMPLEMENTED
(bit-shift-left x n)   ; Left shift                     — NOT YET IMPLEMENTED
(bit-shift-right x n)  ; Right shift                    — NOT YET IMPLEMENTED
```

#### Core Functions - Functions

**Status: Partially implemented**

```clojure
;; Function composition
(comp & fs)            ; Compose functions               — NOT YET IMPLEMENTED
(partial f & args)     ; Partial application             — NOT YET IMPLEMENTED
(complement f)         ; Logical complement              — IMPLEMENTED (core.beer)
(constantly x)         ; Return function that returns x  — IMPLEMENTED (core.beer)
(identity x)           ; Identity function               — IMPLEMENTED (core.beer)

;; Application
(apply f args)         ; Apply function to arguments     — IMPLEMENTED (native)
(juxt & fs)            ; Juxtaposition                   — NOT YET IMPLEMENTED

;; Memoization
(memoize f)            ; Memoize function results        — NOT YET IMPLEMENTED
```

#### Core Functions - Control Flow

**Status: Partially implemented**

```clojure
;; Conditionals (macros)
(when test & body)     ; Execute body if test true       — IMPLEMENTED (core.beer)
(when-not test & body) ; Execute body if test false      — IMPLEMENTED (core.beer)
(if-let [x expr] then else) ; Bind and test             — NOT YET IMPLEMENTED
(when-let [x expr] & body)  ; Bind and test             — NOT YET IMPLEMENTED
(cond & clauses)       ; Multi-way conditional           — IMPLEMENTED (core.beer)
(condp pred expr & clauses) ; Conditional with predicate — NOT YET IMPLEMENTED
(case expr & clauses)  ; Match expression                — NOT YET IMPLEMENTED

;; Iteration (macros)
(doseq [x coll] body)  ; Iterate for side effects       — NOT YET IMPLEMENTED
(dotimes [i n] body)   ; Iterate n times                 — NOT YET IMPLEMENTED
(while test & body)    ; While loop                      — NOT YET IMPLEMENTED
(for [x coll] expr)    ; List comprehension              — NOT YET IMPLEMENTED

;; Threading (macros)
(-> x & forms)         ; Thread-first                    — IMPLEMENTED (core.beer)
(->> x & forms)        ; Thread-last                     — IMPLEMENTED (core.beer)
(as-> x name & forms)  ; Thread with explicit name       — NOT YET IMPLEMENTED
(some-> x & forms)     ; Thread-first, stop on nil       — NOT YET IMPLEMENTED
```

#### Core Functions - I/O

**Status: Partially implemented**

```clojure
;; Output
(print & args)         ; Print args (no newline)         — IMPLEMENTED (native)
(println & args)       ; Print args with newline         — IMPLEMENTED (native)
(pr & args)            ; Print readable (no newline)     — NOT YET IMPLEMENTED
(prn & args)           ; Print readable with newline     — NOT YET IMPLEMENTED
(printf fmt & args)    ; Formatted print                 — NOT YET IMPLEMENTED

;; String conversion
(str & args)           ; Convert to string               — IMPLEMENTED (native)
(pr-str x)             ; Readable string                 — IMPLEMENTED (native)
(print-str x)          ; Display string                  — NOT YET IMPLEMENTED
(prn-str x)            ; Readable string + newline       — NOT YET IMPLEMENTED

;; Reading
(read)                 ; Read from *in*                  — NOT YET IMPLEMENTED
(read-line)            ; Read line from *in*             — NOT YET IMPLEMENTED
(read-string s)        ; Read from string                — NOT YET IMPLEMENTED
```

#### Core Functions - Namespace Operations

**Status: Mostly not yet implemented** — Only `def`, `defn`, and `defmacro` work. Namespace management is minimal.

```clojure
;; Namespace management
(ns name & references) ; Declare namespace
(in-ns name)           ; Switch to namespace
(require & libs)       ; Load libraries
(use & libs)           ; Load and refer
(import & classes)     ; Import classes (for FFI)
(refer ns & filters)   ; Refer symbols from namespace

;; Namespace queries
(all-ns)               ; List all namespaces
(find-ns name)         ; Find namespace by name
(ns-name ns)           ; Get namespace name
(ns-publics ns)        ; Public vars in namespace
(ns-interns ns)        ; All vars in namespace
(ns-aliases ns)        ; Namespace aliases

;; Var operations
(def name value)       ; Define var
(defn name [args] body) ; Define function (macro)
(defmacro name [args] body) ; Define macro
(var-get v)            ; Get var value
(var-set v val)        ; Set var value (for dynamic vars)
(alter-var-root v f)   ; Alter var root
(binding [var val] body) ; Dynamic binding
```

#### Core Functions - Destructuring

**Status: Partially implemented** — Sequential destructuring with `& rest` and `:as` works in `let`. Map destructuring is not yet implemented.

Destructuring works in `let`, `fn`, `defn`, etc:

```clojure
;; Sequential destructuring
(let [[a b c] [1 2 3]]
  (+ a b c))           ; => 6

;; With rest
(let [[first & rest] [1 2 3 4]]
  [first rest])        ; => [1 (2 3 4)]

;; Map destructuring
(let [{:keys [a b]} {:a 1 :b 2}]
  (+ a b))             ; => 3

;; With defaults
(let [{:keys [a b] :or {b 10}} {:a 1}]
  (+ a b))             ; => 11

;; Nested
(let [{:keys [a] [b c] :seq} {:a 1 :seq [2 3]}]
  (+ a b c))           ; => 6
```

### String Library (beerlang.string)

**Status: Not yet implemented — planned for future**

```clojure
;; Case conversion
(upper-case s)         ; Convert to uppercase
(lower-case s)         ; Convert to lowercase
(capitalize s)         ; Capitalize first letter

;; Trimming
(trim s)               ; Trim whitespace
(trim-newline s)       ; Trim trailing newline
(triml s)              ; Trim left
(trimr s)              ; Trim right

;; Splitting/Joining
(split s re)           ; Split by regex
(split-lines s)        ; Split by newlines
(join sep coll)        ; Join with separator
(join coll)            ; Join without separator

;; Searching
(index-of s substr)    ; Find substring index
(last-index-of s substr) ; Find last occurrence
(includes? s substr)   ; Does s contain substr?
(starts-with? s prefix) ; Starts with prefix?
(ends-with? s suffix)  ; Ends with suffix?

;; Modification
(replace s match replacement) ; Replace
(replace-first s match replacement) ; Replace first
(reverse s)            ; Reverse string

;; Formatting
(format fmt & args)    ; Format string (printf-style)
```

### Async Library (beerlang.async)

**Status: Not yet implemented — planned for future**

Concurrency primitives for cooperative multitasking. Initial version focuses on essential, proven patterns.

```clojure
;; Task management
(spawn f)              ; Spawn new task
(spawn-link f)         ; Spawn linked task (failures propagate)
(await task)           ; Wait for task completion
(await task timeout)   ; Wait with timeout

;; Channels (CSP-style - core primitive)
(chan)                 ; Unbuffered channel
(chan n)               ; Buffered channel (size n)
(>! ch val)            ; Put (yields if full)
(<! ch)                ; Take (yields if empty)
(>!! ch val)           ; Blocking put (rarely needed)
(<!! ch)               ; Blocking take (rarely needed)
(close! ch)            ; Close channel
(alts! & channels)     ; Select from multiple channels
(timeout ms)           ; Timeout channel

;; Promises/Futures
(promise)              ; Create promise
(deliver p val)        ; Deliver promise value
(future & body)        ; Execute in task, return promise

;; Atoms (simple thread-safe shared state)
(atom x)               ; Create atom
(deref a)              ; Dereference (also @a)
(reset! a val)         ; Reset value
(swap! a f & args)     ; Atomic swap with function
(compare-and-set! a old new) ; CAS operation
```

**Note on Refs and Agents:**

These are **not included** in the initial version:

- **Refs (STM)**: Software Transactional Memory is complex to implement correctly:
  - Transaction log management
  - Conflict detection and retry logic
  - MVCC (Multi-Version Concurrency Control)
  - Deadlock prevention
  - Significant VM complexity

- **Agents**: Simpler than refs but still require:
  - Per-agent action queue
  - Dispatch infrastructure
  - Error propagation mechanism

**Alternative approaches:**
- **Atoms** handle most shared state needs (simple CAS)
- **Channels** handle task coordination and communication
- **Message passing** via channels is often cleaner than shared mutable state
- Can be added in future if proven necessary

**Example - Using atoms instead of refs:**

```clojure
;; Coordinated state with atoms (manual coordination)
(def account-a (atom 100))
(def account-b (atom 50))

(defn transfer [from to amount]
  ;; Manual coordination (not atomic across accounts)
  (swap! from - amount)
  (swap! to + amount))

;; Or use channels for coordination
(defn transfer-via-channel [from to amount]
  (let [ch (chan)]
    (spawn
      (>! from [:withdraw amount ch])
      (when (<! ch)
        (>! to [:deposit amount ch])))))
```

### Math Library (beerlang.math)

**Status: Not yet implemented — planned for future**

```clojure
;; Constants
Math/PI                ; π
Math/E                 ; e

;; Trigonometry
(sin x)                ; Sine
(cos x)                ; Cosine
(tan x)                ; Tangent
(asin x)               ; Arc sine
(acos x)               ; Arc cosine
(atan x)               ; Arc tangent
(atan2 y x)            ; Arc tangent (two-arg)

;; Exponential/Logarithmic
(exp x)                ; e^x
(log x)                ; Natural log
(log10 x)              ; Base-10 log
(pow x y)              ; x^y
(sqrt x)               ; Square root

;; Rounding
(floor x)              ; Floor
(ceil x)               ; Ceiling
(round x)              ; Round to nearest
(abs x)                ; Absolute value
(signum x)             ; Sign (-1, 0, 1)

;; Random
(rand)                 ; Random float 0-1
(rand-int n)           ; Random int 0..n-1
(rand-nth coll)        ; Random element
(shuffle coll)         ; Shuffle collection
```

### Regex Library (beerlang.regex)

**Status: Not yet implemented — planned for future**

Regular expressions via **FFI to native regex library** (PCRE or POSIX regex). Complex regex engines are non-trivial to implement from scratch, so we leverage existing battle-tested libraries.

```clojure
;; Pattern creation
(re-pattern s)         ; Compile pattern (FFI to PCRE/regex.h)
#"pattern"             ; Reader macro (compiles at read-time)

;; Matching
(re-matches re s)      ; Full match (returns match or nil)
(re-find re s)         ; Find first match
(re-seq re s)          ; Lazy sequence of all matches
(re-groups m)          ; Extract capture groups from match

;; Replacement
(re-replace re s replacement) ; Replace all occurrences
(re-replace-first re s replacement) ; Replace first occurrence
```

**Implementation approach:**

```c
// Pattern object (wraps PCRE regex)
struct Pattern {
    Object      header;
    String*     pattern_str;  // Original pattern
    pcre*       compiled;     // PCRE compiled regex
    int         capture_count; // Number of capture groups
};

// Compile pattern
Pattern* compile_pattern(const char* pattern_str) {
    const char* error;
    int erroffset;

    pcre* compiled = pcre_compile(
        pattern_str,
        0,                    // Options
        &error,
        &erroffset,
        NULL
    );

    if (compiled == NULL) {
        // Compilation error
        return NULL;
    }

    Pattern* p = allocate_pattern();
    p->pattern_str = make_string(pattern_str);
    p->compiled = compiled;

    // Get capture count
    pcre_fullinfo(compiled, NULL, PCRE_INFO_CAPTURECOUNT,
                  &p->capture_count);

    return p;
}

// Match
Value regex_find(Pattern* pat, String* str) {
    int ovector[30];  // Output vector for captures

    int rc = pcre_exec(
        pat->compiled,
        NULL,
        str->data,
        str->header.size,
        0,                // Start offset
        0,                // Options
        ovector,
        30
    );

    if (rc < 0) {
        return make_nil();  // No match
    }

    // Build match result with captures
    return make_match_result(str, ovector, rc);
}
```

**Reader macro implementation:**

```clojure
;; Reader expands #"pattern" at read-time
#"\\d+"  ; => (re-pattern "\\d+")

;; Patterns can be compiled once and reused
(def digit-pattern #"\\d+")
(re-find digit-pattern "abc123")  ; Efficient - pattern already compiled
```

**Alternative for simple cases:**

For very simple patterns, could implement basic matching without full regex:

```clojure
;; Simple string operations (no regex needed)
(string/includes? s "substring")
(string/starts-with? s "prefix")
(string/index-of s "pattern")
```

**Future considerations:**
- Could support multiple regex backends (PCRE, RE2, Rust regex)
- Could implement simple regex subset natively (for bootstrapping)
- Regex compilation could happen at compile-time for literal patterns

### Time Library (beerlang.time)

**Status: Not yet implemented — planned for future**

```clojure
;; Current time
(now)                  ; Current instant
(today)                ; Today's date

;; Construction
(instant ms)           ; From epoch milliseconds
(date year month day)  ; Create date
(time hour min sec)    ; Create time

;; Accessors
(year d)               ; Extract year
(month d)              ; Extract month
(day d)                ; Extract day
(hour t)               ; Extract hour
(minute t)             ; Extract minute
(second t)             ; Extract second

;; Arithmetic
(plus t duration)      ; Add duration
(minus t duration)     ; Subtract duration
(duration ms)          ; Duration from ms

;; Formatting
(format-time t fmt)    ; Format time
(parse-time s fmt)     ; Parse time
```

### Bytecode Library (beerlang.bytecode)

**Status: Not yet implemented — planned for future**

Bytecode metaprogramming utilities:

```clojure
;; Disassembly/Assembly (special forms)
(disasm f)             ; Disassemble function
(asm bytecode)         ; Assemble bytecode

;; Analysis
(bytecode-size f)      ; Count instructions
(instruction-histogram f) ; Frequency of instructions
(stack-depth-analysis bytecode) ; Max stack depth

;; Optimization
(optimize-bytecode bc) ; Apply optimizations
(inline-calls bc)      ; Inline small calls
(constant-fold bc)     ; Fold constants
(dead-code-elim bc)    ; Remove dead code

;; Pattern matching
(match-pattern bc pattern) ; Find instruction patterns
(replace-pattern bc from to) ; Replace patterns
```

### System Library (beerlang.system)

**Status: Not yet implemented — planned for future**

```clojure
;; System info
(os-name)              ; Operating system
(os-version)           ; OS version
(arch)                 ; Architecture (x86_64, arm64, etc.)
(cpu-count)            ; Number of CPUs

;; VM info
(vm-version)           ; Beerlang version
(vm-uptime)            ; VM uptime in ms
(memory-usage)         ; Memory statistics

;; Environment
(env var)              ; Get environment variable
(env)                  ; All environment variables
(property key)         ; System property

;; Process
(exit code)            ; Exit process
(shutdown-hook f)      ; Register shutdown hook
```

### Test Library (beerlang.test)

**Status: Not yet implemented — planned for future**

```clojure
;; Assertions
(is (= actual expected)) ; Assert equality
(is (thrown? Exception expr)) ; Assert exception

;; Test definition
(deftest test-name
  (is (= 1 1)))

;; Running tests
(run-tests)            ; Run all tests in namespace
(run-tests ns)         ; Run tests in namespace
(run-all-tests)        ; Run all tests

;; Fixtures
(use-fixtures :once fixture) ; Run once per namespace
(use-fixtures :each fixture) ; Run per test
```

### Implementation Notes

**Native vs Pure Beerlang:**

- **Native (C)**: Low-level operations (arithmetic, type checking, I/O)
- **Pure Beerlang**: Higher-level functions built on natives (map, filter, reduce)
- **Macros**: Control flow, destructuring (compile-time expansion)

**Lazy Sequences:**

**Status: Not yet implemented — planned for future**

Lazy sequences use **traditional thunks** (delayed computation), NOT `yield`. This keeps lazy evaluation separate from cooperative multitasking.

**Implementation approach:**

```clojure
;; lazy-seq macro wraps computation in a thunk
(defmacro lazy-seq [& body]
  `(LazySeq. (fn [] ~@body)))

;; Example: infinite range
(defn range
  ([] (range 0))
  ([start]
    (lazy-seq
      (cons start (range (inc start))))))

;; Usage
(take 5 (range))       ; Only computes first 5 elements
(take 5 (map inc (range))) ; Lazy map - composes lazily
```

**Under the hood:**

```c
// Lazy sequence object
struct LazySeq {
    Object   header;
    Value    thunk;      // Zero-arg function (or NULL if realized)
    Value    realized;   // Cached result (or NULL if not yet computed)
    bool     is_realized; // Have we computed this yet?
};

// Force lazy sequence
Value realize_lazy_seq(LazySeq* ls) {
    if (ls->is_realized) {
        return ls->realized;  // Return cached value
    }

    // Call thunk to compute value
    Value result = call_function(ls->thunk, 0, NULL);

    // Cache result
    ls->realized = result;
    ls->is_realized = true;

    // Release thunk (allow GC)
    ls->thunk = make_nil();

    return result;
}
```

**Why NOT use yield?**

1. **Different concerns:**
   - **Lazy evaluation**: Delay computation until needed (within a task)
   - **Cooperative yielding**: Give other tasks CPU time (between tasks)
   - Mixing them confuses two orthogonal concepts

2. **Lazy seqs are typically consumed quickly:**
   ```clojure
   (reduce + (take 1000 (range)))  ; Computed in one go
   ```

3. **If expensive, use tasks explicitly:**
   ```clojure
   ;; Compute lazily within a task
   (spawn
     (reduce + (take 1000000 (range))))

   ;; Parallel processing
   (let [tasks (map spawn
                    [(take 1M (range 0))
                     (take 1M (range 1M))])]
     (map await tasks))
   ```

4. **Simpler implementation:**
   - Just thunks + caching
   - No VM integration needed
   - Proven Clojure approach

**Automatic yielding consideration:**

We COULD insert automatic yields for long-running lazy sequences:

```clojure
;; Hypothetical: auto-yield every N iterations
(defn range-with-yield [start]
  (lazy-seq
    (when (= (rem start 1000) 0)
      (yield))  ; Every 1000th element
    (cons start (range-with-yield (inc start)))))
```

But this is **not recommended** because:
- Adds complexity
- Lazy seqs cross task boundaries (confusing)
- Better to explicitly spawn if computation is heavy

**Best practice:**

```clojure
;; Lazy within a task (normal)
(take 1000 (range))

;; Heavy computation? Use tasks explicitly
(let [result-task (spawn
                    (reduce + (range 1000000)))]
  (await result-task))

;; Process in chunks with tasks
(defn parallel-reduce [n chunk-size f init]
  (let [chunks (partition chunk-size (range n))
        tasks (map #(spawn (reduce f init %)) chunks)]
    (reduce f init (map await tasks))))
```

**Transducers:** *(Not yet implemented — planned for future)*
```clojure
;; Composable transformation pipelines
(def xf (comp (map inc) (filter even?)))
(transduce xf + 0 [1 2 3 4]) ; => 12
```

**Protocols:** *(Not yet implemented — planned for future)*
```clojure
;; Protocol-based polymorphism
(defprotocol ISeq
  (first [s])
  (rest [s]))
```
