## Memory Management

#### Why Reference Counting Works Well Here

1. **Persistent Data Structures**: Following Clojure's model, all core data structures are immutable and persistent
   - Structural sharing creates predominantly tree-shaped object graphs
   - Cycles are rare by design (unlike mutable object graphs)
   - Most objects have clear ownership hierarchies

2. **Cache Efficiency**: Reference counting aligns with the cache-friendly VM design
   - Objects are freed immediately when unreferenced
   - Memory is reclaimed deterministically
   - No unpredictable GC pauses that could evict VM from cache

3. **Cooperative Multitasking**: Perfect fit with the concurrency model
   - No stop-the-world pauses interrupting cooperative tasks
   - Within a single task, no atomic operations needed for refcounts
   - Predictable performance characteristics

4. **Simple Implementation**: Keeps the VM small and understandable
   - Minimal GC code footprint
   - Easy to reason about and debug
   - No complex tracing algorithms

#### Handling the Challenges

**Cycles**: While rare, cycles can still occur in:
- Closures that capture themselves
- Metadata referencing parent structures
- Explicit cycles in user code

**Solutions**:
- **Weak references** for known cycle-prone patterns (closures, metadata)
- **Optional cycle detector**: Periodic scan for unreachable cycles (can be cooperative)
- **Language design**: Make cycles naturally rare (follow Clojure's lead)

**Cascade Deletes**: Large structure deallocations could cause latency spikes

**Solutions**:
- **Deferred deletion queue**: Amortize large deallocations over time
- **Cooperative yielding**: Yield during cascade deletes of deep structures
- **Threshold-based**: Only defer deletion for structures above size threshold

**Reference Counting Overhead**: Every assignment needs inc/dec operations

**Mitigations**:
- No atomic operations needed within single cooperative task
- VM-level optimizations (elide obvious inc/dec pairs)
- Stack references don't need counting (owned by frame)

#### Current Implementation

1. **Per-object refcount**: 32-bit field in Object header (`uint32_t refcount`)
2. **Stack references ARE counted**: `vm_push` retains (+1), `vm_pop` releases (-1)
3. **STORE_LOCAL**: Retains new value BEFORE releasing old (new may be sub-object of old)
4. **RETURN**: Retains return value before cleanup loop, releases after push
5. **Interned objects**: Symbols and keywords are interned — refcount=1 owned by intern table, callers do NOT release them
6. **Leak tracking**: Build with `make track-leaks`, run with `--dump-leaks` to detect leaks

#### Immortal Function Templates

When code is compiled (either from a file or at the REPL), `fn` forms create **function template objects** that are stored in the compiled code's constants array. These templates serve two roles:

1. **Blueprints for `MAKE_CLOSURE`**: When a `fn` captures variables from an enclosing scope, `OP_MAKE_CLOSURE` creates a fresh function object at runtime. The template's code offset, arity, and name are baked into the bytecode operands — the template object itself isn't used.

2. **Direct values via `PUSH_CONST`**: When a `fn` has no captures, the compiler emits `OP_PUSH_CONST` which pushes the template object directly. The template IS the function — it gets returned, stored in vars, passed to other tasks.

Case 2 creates a lifetime challenge: the template lives in a raw `Value*` array (owned by `load_constants[]`), but user code holds references to it across task boundaries, scheduler ticks, and namespace vars. Any refcount imbalance frees the template while it's still in use.

**Solution: immortal refcount.** Function templates in constants arrays are marked with `REFCOUNT_IMMORTAL` (`UINT32_MAX`). `object_retain` and `object_release` skip immortal objects entirely. The templates live forever alongside their constants arrays (which are already never freed).

This is safe because:
- Constants arrays persist for the lifetime of the program (stored in static `load_units[]` / `load_constants[]`)
- Templates are immutable — sharing them across tasks is safe
- No extra allocations — the template is created once and reused
- Closures (which DO capture values) are created fresh by `MAKE_CLOSURE` with normal refcounting

**Implementation note:** `REFCOUNT_IMMORTAL` uses a sentinel value (`UINT32_MAX`). A dedicated flag bit in the object header would be more robust and avoid any theoretical overflow concern, but the sentinel approach is simpler and sufficient for now.

**Alternative considered: always emit `MAKE_CLOSURE`** even for zero-capture functions. This would give every returned function an independent lifetime, eliminating the template sharing issue entirely. The tradeoff is one allocation per call (e.g., `(map my-fn big-coll)` would allocate N identical function objects). This approach may be worth revisiting if the immortal template semantics prove too coarse — for instance, if a future "unload module" feature needs to reclaim compiled code.

#### Future Enhancements

1. **Weak references**: Special pointer type that doesn't increment refcount (for cycle-prone patterns)
2. **Optional cycle detector**: Periodic scan for unreachable cycles
3. **Deferred deletion**: Queue for cooperative cleanup of large structures
4. **Immortal flag bit**: Replace `REFCOUNT_IMMORTAL` sentinel with a dedicated bit in the object header for cleaner semantics
