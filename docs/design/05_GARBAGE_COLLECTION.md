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

#### Future Enhancements

1. **Weak references**: Special pointer type that doesn't increment refcount (for cycle-prone patterns)
2. **Optional cycle detector**: Periodic scan for unreachable cycles
3. **Deferred deletion**: Queue for cooperative cleanup of large structures
