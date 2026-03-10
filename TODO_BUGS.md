# Known Bugs

## ~~Cons Destructor Not Releasing Car/Cdr~~ (RESOLVED)

**Status:** RESOLVED 2026-01-27

**Root Causes Identified:**

1. **Destructor Table Clearing Issue:**
   - `memory_init()` was clearing the destructor table with `memset(g_destructors, 0, ...)`
   - String, bigint, and cons types use static bool guards to prevent re-registration
   - When tests called `memory_init()` multiple times, the destructor table was cleared but the static guards prevented re-registration
   - Solution: Don't clear destructor table in `memory_init()` - destructors are type-level metadata, not per-test state

2. **Memory Leaks in List-Building Functions:**
   - Functions like `list_from_array`, `list_reverse`, `list_append`, `list_map`, `list_filter` all had the pattern:
     ```c
     result = cons(..., result);  // cons() retains old result, but we overwrite without releasing
     ```
   - This caused intermediate cons cells to have orphaned references
   - Solution: Release old result before overwriting:
     ```c
     Value new_cons = cons(..., result);
     if (is_pointer(result)) {
         object_release(result);
     }
     result = new_cons;
     ```

**Files Modified:**
- `src/memory/alloc.c` - Don't clear destructor table in memory_init()
- `src/types/cons.c` - Fix memory leaks in list_from_array, list_reverse, list_append, list_map, list_filter

**Test Results:**
- All 382 tests passing (38 memory + 55 bigint + 65 cons + 62 string + 52 symbol + 66 fixnum + 44 value)
- No memory leaks detected


## Issue in the repl

Evaluating 
```clojure
(let [q 321] (def ff (fn [x] (+ x q))))
```
in the REPL and then trying to call `(ff 123)` results in an error


## Some issues with loop-recur

~This has been adressed~

Accumulating to a list does not work
```clojure
(loop [i   4
	   acc (list)]
	(if (= i 0)
		acc
		(recur (- i 1) (conj acc i))))
```
It returns an arbitrary object (opcode?) instead of a standard object type


## Issues with loop/recur over sequences

This breaks after the second iteration

```clojure
(loop [x '(1 2 3 4 5)]
  (println "XXX" x)
  (if (empty? x)
    (println "done")
	(recur (rest x))))

```

Other example:

This works:
```clojure
(map (fn [x] (+ x 1)) '(1 2))
```

This breaks:
```clojure
(map (fn [x] (+ x 1)) '(1 2 3))
```
