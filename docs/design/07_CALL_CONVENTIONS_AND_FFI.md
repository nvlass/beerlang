### Calling Conventions and FFI

#### Dual-Stack Architecture

The VM maintains **two separate stacks**:

1. **Value stack** (`vm->stack`): Holds arguments, locals, temporaries, and return values. `stack_pointer` points to the next free slot.
2. **Call frame stack** (`vm->frames`): Holds `CallFrame` structs that track return addresses and caller context. `frame_count` tracks depth.

This separation keeps frame metadata out of the value stack, simplifying stack manipulation and reference counting.

#### CallFrame Structure

Each function call pushes one `CallFrame` onto the frame stack:

```c
typedef struct CallFrame {
    uint32_t return_pc;      /* Return address (PC to resume at) */
    int base_pointer;        /* Base of stack frame (for locals) */
    Value function;          /* Function being executed (for closures) */
    /* Saved caller execution context (restored on return) */
    uint8_t* caller_code;
    int caller_code_size;
    Value* caller_constants;
    int caller_num_constants;
} CallFrame;
```

The frame saves the caller's entire execution context (code pointer, constants) because each compiled function has its own bytecode and constant pool. On return, these are restored so the caller resumes in its own code.

#### Stack Layout for a Function Call

```
Value stack (after ENTER):

High addresses (stack top)
┌─────────────────────────────┐
│   Local n-1 (= nil)         │  ← base_pointer + n_args + n_locals - 1
│   ...                       │
│   Local 0 (= nil)           │  ← base_pointer + n_args
├─────────────────────────────┤
│   Arg n-1                   │  ← base_pointer + n_args - 1
│   ...                       │
│   Arg 1                     │  ← base_pointer + 1
│   Arg 0                     │  ← base_pointer + 0
└─────────────────────────────┘
Low addresses
```

Arguments and locals occupy a contiguous region starting at `base_pointer`. They share the same index space: `LOAD_LOCAL 0` is arg 0, `LOAD_LOCAL n_args` is the first local after the arguments.

#### Calling Sequence

**Caller (before CALL instruction):**
```
1. Push arguments left to right: arg0, arg1, ..., argN
2. Push function object on top
3. Execute CALL <n_args>
```

**CALL instruction (VM):**
```
1. Pop function object from value stack
2. Verify it is callable and check arity
3. For variadic functions (negative arity): collect excess args into a cons list
4. Push a CallFrame onto the frame stack:
   - return_pc = current PC
   - base_pointer = stack_pointer - n_args (where args start)
   - function = the function value
   - caller_code, caller_constants, etc. = current execution context
5. Switch PC and code/constants to the callee's bytecode
```

**ENTER instruction (first instruction in every function body):**
```
ENTER <n_locals>
- Pushes n_locals nil values onto the value stack
- These become the local variable slots after the arguments
```

**Function body:**
- Access arguments: `LOAD_LOCAL 0`, `LOAD_LOCAL 1`, etc.
- Access locals: `LOAD_LOCAL n_args`, `LOAD_LOCAL n_args+1`, etc.
- Access closure variables: `LOAD_CLOSURE 0`, `LOAD_CLOSURE 1`, etc.

**RETURN instruction:**
```
1. Pop return value from value stack
2. Retain return value (increment refcount) — it may alias a local about to be freed
3. Release all locals and args on the value stack (pop down to base_pointer)
4. Pop CallFrame from frame stack
5. Restore caller execution context (code, constants, PC)
6. Push return value onto value stack
7. Release the extra retain from step 2
```

#### Tail Call Optimization (Implicit)

Beerlang implements **proper tail calls** as a fundamental language feature. The compiler automatically detects calls in tail position and emits `TAIL_CALL` instead of `CALL`.

**Tail Position Definition:**

A call is in tail position when its return value is directly returned:

```clojure
(fn [x]
  (if (< x 10)
    (foo x)         ; TAIL POSITION - result directly returned
    (bar x)))       ; TAIL POSITION - result directly returned

(fn [x]
  (do
    (println x)     ; NOT tail position
    (foo x)))       ; TAIL POSITION - last expression in do

(fn [x]
  (let [y (+ x 1)]
    (factorial y))) ; TAIL POSITION - last in let body

(fn [x]
  (+ 1 (foo x)))   ; NOT tail position - foo's result used by +
```

**Compiler Tail Position Detection:**

The compiler tracks tail position with a boolean flag during compilation. Tail position propagates into:
- Both branches of `if`
- The last expression of `do`
- The body of `let*`
- The body of `fn`

Tail position does NOT propagate into:
- Function arguments
- Test expressions of `if`
- Binding values in `let*`
- Non-final expressions of `do`

**TAIL_CALL instruction:**

`TAIL_CALL <n_args>` reuses the current call frame instead of pushing a new one:

```
1. Pop function object
2. Save new arguments temporarily
3. Release old locals and args (clean the frame)
4. Copy new arguments into the frame starting at base_pointer
5. Update the CallFrame's function field
6. Switch to callee's bytecode and PC
7. (ENTER will allocate new locals)
```

The return address and caller context in the `CallFrame` are preserved from the original caller, so when the tail-called function returns, it returns directly to the original caller.

**Benefits:**
- Works for ALL tail calls, not just self-recursion (unlike Clojure's `recur`)
- Mutual recursion works with constant stack space
- No special syntax required — the compiler handles it automatically
- REPL code gets the same optimization

#### Named Function Self-Recursion (OP_LOAD_SELF)

Named `fn` forms like `(fn factorial [n] ...)` support self-recursion through an extra local slot:

```
1. The function name is added as an additional local slot after the parameters
2. After ENTER, the compiler emits OP_LOAD_SELF + OP_STORE_LOCAL to populate it
3. OP_LOAD_SELF pushes the current CallFrame's function value onto the stack
4. Symbol resolution finds the local name before checking namespace vars
```

This allows recursion to work even before `def` completes binding the var.

#### Variadic Functions

Variadic functions have a negative arity value: `-(required_args + 1)`.

During `CALL`, if the function is variadic:
- The required arguments are left as-is
- Excess arguments are collected into a cons list (linked list)
- The cons list is placed in the parameter slot for the variadic argument

For example, `(fn [a b & rest] ...)` has arity `-3` (two required args + 1). Calling with `(f 1 2 3 4)` results in: local 0 = 1, local 1 = 2, local 2 = (3 4).

#### Closure Access

Closures capture **values** (not variables) from outer scopes at creation time:

```clojure
(let [x 10]
  (fn [y] (+ x y)))  ; x is captured by value
```

**MAKE_CLOSURE instruction:**
```
MAKE_CLOSURE <const_idx> <n_closed>
1. Pop n_closed values from the value stack (these are the captured values)
2. Create a new Function object with a closed[] array holding the captured values
3. Captured values are retained (refcount incremented — now owned by the closure)
4. Push the new function onto the value stack
```

**LOAD_CLOSURE instruction:**
```
LOAD_CLOSURE <idx>
1. Get the function object from the current CallFrame
2. Push function->closed[idx] onto the value stack
```

Since beerlang values are immutable, capturing by value is equivalent to capturing by reference and avoids the need for mutable cells or indirection.

#### Exception Handling

Exception handling uses a **separate handler stack** (`vm->handlers`, tracked by `handler_count`):

```c
typedef struct ExceptionHandler {
    uint32_t catch_pc;       /* PC of catch block (absolute) */
    int stack_pointer;       /* SP to restore on throw */
    int frame_count;         /* Frame count to restore on throw */
} ExceptionHandler;
```

**PUSH_HANDLER instruction:**
```
PUSH_HANDLER <catch_pc>
1. Push an ExceptionHandler onto the handler stack
2. Record: catch_pc, current stack_pointer, current frame_count
```

**THROW instruction:**
```
1. Pop the exception value (must be a hashmap, e.g. {:type :error :message "..."})
2. Pop the top ExceptionHandler
3. Unwind frames: while frame_count > handler's frame_count, perform
   RETURN-like cleanup (release locals and args for each frame)
4. Restore stack_pointer to handler's saved value
5. Store exception value for the catch block
6. Jump to catch_pc
```

**POP_HANDLER instruction:**
```
POP_HANDLER
- Decrements handler_count (normal exit from try block, no exception thrown)
```

> **Note:** `finally` is not yet implemented.

#### Reference Counting and the Stack

**Stack operations DO perform reference counting:**
- `vm_push` retains the value (increments refcount)
- `vm_pop` releases the value (decrements refcount)

This ensures correct lifetime management as values move on and off the stack.

**Critical ordering rules:**
- `STORE_LOCAL`: Retain the new value BEFORE releasing the old value. The new value may be a sub-object of the old value; releasing first could free it prematurely.
- `RETURN`: Retain the return value before the cleanup loop releases locals, then release the extra retain after pushing the result.

**Closure capture:**
When `MAKE_CLOSURE` creates a closure, captured values are retained (refcount incremented) since they are now owned by the closure's `closed[]` array. When the closure is freed, captured values are released.

**Interned symbols and keywords:**
`symbol_intern` and `keyword_intern` return references owned by the intern table. Do NOT release them — their lifetime is managed by the table, not by the caller.

#### Native Function Calls

Native (C) functions are called inline during the `CALL` instruction when the function object is a native function:

```c
typedef Value (*NativeFn)(VM* vm, int n_args, Value* args);
```

**Calling sequence:**
```
1. CALL detects the function is native
2. Collect a pointer to the args on the stack (args = &stack[sp - n_args])
3. Call the C function: result = native_fn(vm, n_args, args)
4. Pop args and the function from the value stack
5. Push result onto the value stack
```

Native functions receive a direct pointer to the arguments on the stack. They do not pop or push the stack themselves — the VM handles that.

**Temp VM pattern (for calling bytecode functions from C):**

Functions like `expand_macro`, `apply`, and `macroexpand-1` need to call bytecode functions from C code. They use a temporary VM:

```
1. Create a mini bytecode: PUSH_CONST args..., PUSH_CONST fn, CALL n, HALT
2. Run in a temporary VM
3. Retain the result before freeing the temp VM
```

---

### Future / Not Yet Implemented

The following features are planned but not yet present in the implementation:

#### Cooperative Yielding

The `YIELD` instruction will allow cooperative multitasking:

```c
void execute_YIELD(VM* vm) {
    vm->state = TASK_YIELDED;
    // Scheduler resumes by continuing execution
    // All state preserved in stack frames
}
```

**Planned yield point insertion:**
- At the start of loop iterations
- In long-running computations
- After N bytecode instructions (back-edge threshold)

#### FFI / Shared Library Loading

Planned instructions for calling external shared libraries:
- `LOAD_LIB` — Load a shared library (dlopen)
- `CALL_NATIVE` — Call a foreign function with type marshalling

This will allow beerlang to call C libraries directly, with FFI calls dispatched on separate threads to avoid blocking the cooperative scheduler.

#### Debug Mode for TCO

A flag to disable tail call optimization, preserving full stack traces for debugging:

```clojure
(set! *debug-mode* true)  ; Emit CALL even in tail position
```

#### Call Performance Optimizations (Future)

- **Inline caching**: Cache function objects at call sites
- **Monomorphic calls**: Specialize for single function at a call site
- **JIT compilation**: Inline small functions, register allocation, devirtualize calls
