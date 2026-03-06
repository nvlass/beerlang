### REPL Execution Flow

```
Read → Compile → Execute → Print
```

All code entered at the REPL is compiled to bytecode before execution. There is no interpreter — the REPL uses the same compilation pipeline as file-based code.

**Simple expression:**
```clojure
user=> (+ 1 2)
```

Internally:
1. **Read**: Parse `(+ 1 2)` into S-expression `(+ 1 2)`
2. **Compile** to bytecode:
   ```
   PUSH_INT 1          ; Push args left-to-right
   PUSH_INT 2
   LOAD_VAR <idx-of-+> ; Push function last
   CALL 2              ; Call with 2 args
   HALT                ; Top-level code ends with HALT, not RETURN
   ```
3. **Execute**: Run bytecode on VM
4. **Print**: `3`

**Function definition:**
```clojure
user=> (defn square [x] (* x x))
```

Internally:
1. **Read**: Parse entire `defn` form
2. **Macro expand**: `defn` → `(def square (fn square [x] (* x x)))`
3. **Compile**:
   - Emit JUMP to skip function body
   - Compile `(fn square [x] (* x x))` body inline (ENTER, LOAD_SELF, body, RETURN)
   - Patch JUMP target
   - Emit PUSH_CONST with function object (or MAKE_CLOSURE if captures needed)
   - Emit STORE_VAR to bind to `square` in namespace
4. **Execute**: Create function, bind to `user/square`
5. **Print**: `#<fn square>`

**Interactive code:**
```clojure
user=> (do
         (println "Hello")
         (let [x 10]
           (* x x)))
```

Internally:
1. **Read**: Parse entire expression
2. **Compile** to bytecode (top-level `let` wraps in synthetic function):
   ```
   ; do body:
   PUSH_CONST <"Hello">   ; arg for println
   LOAD_VAR <println>     ; function
   CALL 1
   POP                    ; discard println result
   ; let wraps in synthetic fn:
   JUMP <skip>
   ENTER 1                ; 1 local for x
   PUSH_INT 10
   STORE_LOCAL 0          ; x = 10
   LOAD_LOCAL 0           ; x
   LOAD_LOCAL 0           ; x
   LOAD_VAR <*>           ; multiply fn
   CALL 2
   RETURN
   <skip>:
   PUSH_CONST <fn-obj>    ; the synthetic function
   CALL 0                 ; call with 0 args
   HALT
   ```
3. **Execute**: Run bytecode — prints "Hello", returns 100
4. **Print**: `100`

### Benefits of Compile-Everything Approach

1. **Consistency**: Same semantics in REPL and compiled files
   - Tail calls work the same way
   - Same performance characteristics
   - Same error messages

2. **Performance**: No interpretation overhead
   - Even REPL code runs at VM speed
   - Bytecode is cached and reusable
   - No mode switch between REPL and "real" code

3. **Simplicity**: Single code path
   - No separate interpreter
   - Smaller VM implementation
   - Easier to reason about and test

4. **Incremental compilation**: Each form is independently compiled
   - Fast feedback cycle
   - Can redefine functions interactively
   - No need to reload entire files

5. **Natural tail calls**:
   - Compiler detects tail position during REPL compilation
   - Works exactly the same as in file-based code
   - No special REPL mode needed

### REPL Implementation

The REPL loop (in `src/repl/main.c`):

1. Read a line of input into a buffer
2. Parse with the reader (`reader_read`)
3. Compile with the compiler (`compile`) → returns `CompiledCode*`
4. Load bytecode and constants into the VM (`vm_load_code`, `vm_load_constants`)
5. Run the VM (`vm_run`)
6. If no error, pop and print the result (`value_print`)
7. Keep `CompiledCode*` alive (function objects reference its bytecode)

The VM is reused across REPL iterations. Compiled code units are kept alive because function objects stored in namespace vars reference their bytecode/constants pointers. The namespace system persists across iterations, allowing incremental definition building.

### Core Library Loading

At startup, the REPL loads `lib/core.beer` which defines essential macros (`defn`, `defmacro`, `when`, `cond`, `and`, `or`, `let`, `->`, `->>`) and standard library functions. These are compiled and executed before the first user prompt.
