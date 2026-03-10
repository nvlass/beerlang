# Future Optimizations

## Fixnum Caching (Flyweight Pattern)

### Motivation
Many programs use small integers repeatedly (0, 1, -1, etc.). Languages like Python, Java, and Ruby cache small integers to:
- Reduce repeated tagging/untagging operations
- Enable pointer equality for common values
- Improve cache locality

### Implementation Ideas

**Option 1: Pre-allocated Fixnum Cache**
```c
// Cache common fixnums: -128 to 255 (similar to Java's Integer cache)
#define FIXNUM_CACHE_MIN -128
#define FIXNUM_CACHE_MAX 255
#define FIXNUM_CACHE_SIZE (FIXNUM_CACHE_MAX - FIXNUM_CACHE_MIN + 1)

static Value g_fixnum_cache[FIXNUM_CACHE_SIZE];

void fixnum_init_cache(void) {
    for (int i = 0; i < FIXNUM_CACHE_SIZE; i++) {
        g_fixnum_cache[i] = make_fixnum(FIXNUM_CACHE_MIN + i);
    }
}

static inline Value make_fixnum_cached(int64_t n) {
    if (n >= FIXNUM_CACHE_MIN && n <= FIXNUM_CACHE_MAX) {
        return g_fixnum_cache[n - FIXNUM_CACHE_MIN];
    }
    return make_fixnum(n);
}
```

**Option 2: Compile-Time Constants**
Use preprocessor to define commonly-used fixnums:
```c
#define FIXNUM_ZERO  make_fixnum(0)
#define FIXNUM_ONE   make_fixnum(1)
#define FIXNUM_MINUS_ONE make_fixnum(-1)
```

**Option 3: Hybrid Approach**
- Use Option 2 for very common values (0, 1, -1)
- Use Option 1 for broader range when profiling shows benefit

### Benchmarking Needed
Before implementing, profile to see:
- How often small fixnums are created (use instrumentation)
- Cache hit rates with different ranges
- Memory vs. performance tradeoff

### References
- Python: Caches integers -5 to 256 (see Objects/longobject.c)
- Java: Integer cache -128 to 127 (configurable via -XX:AutoBoxCacheMax)
- Ruby: Caches fixnums (FIXNUM_FLAG handling)
- V8: Small integer (Smi) optimizations

### Related Optimizations
- **String interning**: Already implemented for symbols/keywords
- **Cons cell pooling**: Could pool frequently allocated/freed cons cells
- **Small object allocation**: Consider separate allocator for small objects
