#### Philosophy

In Lisp tradition, code is data. Beerlang extends this to the bytecode level:
- Functions can be disassembled into data structures (lists of instructions)
- Bytecode can be inspected, analyzed, and modified
- Modified bytecode can be reassembled into executable functions
- Enables runtime code generation and optimization

#### disasm - Disassemble Bytecode

The `disasm` special form takes a function and returns its bytecode as a data structure:

```clojure
;; Define a function
(defn add-one [x]
  (+ x 1))

;; Disassemble it
(disasm add-one)

;; Returns:
;; [[ENTER 0]
;;  [LOAD_VAR <idx-of-+>]
;;  [LOAD_LOCAL 0]
;;  [PUSH_INT 1]
;;  [CALL 2]
;;  [RETURN]]
```

**Disassembly Format:**

Each instruction is represented as a vector:
- First element: instruction name (keyword or symbol)
- Remaining elements: operands

```clojure
;; Simple instructions (no operands)
[:RETURN]
[:ADD]
[:YIELD]

;; Instructions with operands
[:PUSH_INT 42]
[:LOAD_VAR 5]
[:JUMP_IF_FALSE 10]
[:CALL 3]
[:ENTER 2]

;; Instructions with labels (resolved to offsets)
[:JUMP :loop-start]
[:LABEL :loop-start]
```

**What can be disassembled:**
- Regular functions: `(disasm my-fn)`
- Anonymous functions: `(disasm (fn [x] (* x x)))`
- Closures: Shows captured values
- Vars: `(disasm #'user/foo)` - disassembles var's value

**Metadata in disassembly:**

```clojure
(disasm add-one)

;; Returns with metadata:
;; ^{:arity 1
;;   :n-locals 0
;;   :code-size 6
;;   :var #'user/add-one}
;; [[ENTER 0]
;;  [LOAD_VAR 0]
;;  [LOAD_LOCAL 0]
;;  [PUSH_INT 1]
;;  [CALL 2]
;;  [RETURN]]
```

#### asm - Assemble Bytecode

The `asm` special form takes a bytecode data structure and returns an executable function:

```clojure
;; Create a function from bytecode
(def my-fn
  (asm [[ENTER 0]
        [LOAD_LOCAL 0]
        [INC]
        [RETURN]]))

(my-fn 5)  ; => 6
```

**Assembly Features:**

1. **Labels and jumps:**
   ```clojure
   (asm [[ENTER 1]
         [:LABEL :loop]
         [LOAD_LOCAL 0]
         [PUSH_INT 0]
         [GT]
         [JUMP_IF_FALSE :end]
         [LOAD_LOCAL 0]
         [DEC]
         [STORE_LOCAL 0]
         [JUMP :loop]
         [:LABEL :end]
         [LOAD_LOCAL 1]
         [RETURN]])
   ```

2. **Arity specification:**
   ```clojure
   ;; Fixed arity
   (asm {:arity 2} bytecode)

   ;; Variadic
   (asm {:arity -1} bytecode)
   ```

3. **Closure creation:**
   ```clojure
   (let [x 10]
     ;; Create closure that captures x
     (asm {:captures [x]}
          [[ENTER 0]
           [LOAD_CLOSURE 0]  ; Load captured x
           [LOAD_LOCAL 0]     ; Load argument
           [ADD]
           [RETURN]]))
   ```

4. **Validation:**
   - `asm` validates bytecode structure
   - Checks instruction operands
   - Verifies label references
   - Checks stack depth consistency
   - Reports errors with position information

#### Use Cases

**1. Debugging and Learning:**

```clojure
;; See what the compiler generates
(defn factorial [n]
  (if (< n 2)
    1
    (* n (factorial (- n 1)))))

(println (disasm factorial))

;; Understand tail call optimization
(defn factorial-tr [n acc]
  (if (< n 2)
    acc
    (factorial-tr (- n 1) (* n acc))))

(println (disasm factorial-tr))
;; Look for TAIL_CALL instruction
```

**2. Manual Optimization:**

```clojure
;; Original function
(defn add-one [x]
  (+ x 1))

;; Disassemble and optimize
(def optimized-bytecode
  (-> (disasm add-one)
      ;; Replace [PUSH_INT 1] [ADD] with [INC]
      (replace-sequence [[PUSH_INT 1] [ADD]] [[INC]])))

(def add-one-optimized (asm optimized-bytecode))
```

**3. Runtime Code Generation:**

```clojure
;; Generate specialized functions at runtime
(defn make-adder [n]
  (asm [[ENTER 0]
        [LOAD_LOCAL 0]
        [PUSH_INT ~n]  ; Bake in the constant
        [ADD]
        [RETURN]]))

(def add-10 (make-adder 10))
(add-10 5)  ; => 15
```

**4. Peephole Optimization:**

```clojure
(defn optimize-bytecode [bytecode]
  (-> bytecode
      ;; [PUSH_INT n] [PUSH_INT 1] [ADD] -> [PUSH_INT n+1]
      (fold-constants)
      ;; [LOAD_LOCAL i] [POP] -> []
      (remove-dead-loads)
      ;; [JUMP :L] [:LABEL :L] -> [:LABEL :L]
      (remove-useless-jumps)))
```

**5. Bytecode Analysis:**

```clojure
;; Analyze instruction frequency
(defn instruction-histogram [fn]
  (->> (disasm fn)
       (map first)
       (frequencies)
       (sort-by second >)))

;; Find all function calls
(defn find-calls [fn]
  (->> (disasm fn)
       (filter #(= (first %) :CALL))
       (map second)))

;; Estimate stack depth
(defn max-stack-depth [bytecode]
  (reduce (fn [[depth max-depth] instr]
            (let [new-depth (+ depth (stack-effect instr))]
              [new-depth (max max-depth new-depth)]))
          [0 0]
          bytecode))
```

**6. JIT Preparation:**

```clojure
;; Mark hot functions for JIT compilation
(defn mark-hot-path [fn]
  (let [bc (disasm fn)]
    (asm (with-meta bc {:jit-compile true}))))
```

#### Safety and Validation

**Type Safety:**
- `asm` validates instruction operands
- Checks stack depth consistency
- Verifies jump targets exist
- Ensures local variable indices are valid

**Invalid bytecode:**

```clojure
;; Error: stack underflow
(asm [[ADD]         ; No values on stack!
      [RETURN]])

;; Error: undefined label
(asm [[JUMP :nowhere]
      [RETURN]])

;; Error: invalid operand
(asm [[PUSH_INT "not a number"]
      [RETURN]])
```

**Sandboxing:**
- Assembled bytecode runs in same sandbox as normal code
- No additional privileges
- Subject to same resource limits
- Can't escape VM

#### REPL Integration

```clojure
user=> (defn square [x] (* x x))
#'user/square

user=> (disasm square)
[[ENTER 0]
 [LOAD_VAR 2]
 [LOAD_LOCAL 0]
 [LOAD_LOCAL 0]
 [CALL 2]
 [RETURN]]

user=> (def fast-square
         (asm [[ENTER 0]
               [LOAD_LOCAL 0]
               [LOAD_LOCAL 0]
               [MUL]
               [RETURN]]))
#'user/fast-square

user=> (fast-square 5)
25
```

#### Implementation Notes

**Disassembly:**
- Function object contains bytecode pointer
- Disassembler walks bytecode, decodes instructions
- Resolves var indices to names
- Converts offsets to labels for readability

**Assembly:**
- Parser validates instruction format
- Label resolution pass
- Stack depth analysis
- Creates Function object with bytecode
- Returns executable function value

**Performance:**
- `disasm` is not optimized (debugging tool)
- `asm` validates, so has overhead
- Assembled functions run at normal VM speed
- No runtime overhead for assembled bytecode

#### Future Extensions

1. **Bytecode optimization passes:**
   ```clojure
   (asm-optimize bytecode)  ; Apply standard optimizations
   ```

2. **Bytecode transformations:**
   ```clojure
   (instrument bytecode)    ; Add profiling instrumentation
   (inline-calls bytecode)  ; Inline small function calls
   ```

3. **Pattern matching on bytecode:**
   ```clojure
   (match-bytecode bytecode
     [:PUSH_INT ?n :PUSH_INT 1 :ADD]
     => [:PUSH_INT (inc ?n)])
   ```

4. **Bytecode libraries:**
   - Standard optimization passes
   - Analysis tools
   - Code generation helpers
