# Foreign Function Interface (FFI)

Beerlang's FFI allows calling C functions from shared libraries (or statically linked functions on embedded targets) using a macro-based type DSL built on minimal VM additions.

## Design Philosophy

- **Minimal VM changes**: Only a handful of new natives and one new type
- **DSL in beerlang**: Type declarations, struct layout, and convenience macros are pure beerlang
- **libffi for portability**: Calling convention handling delegated to [libffi](https://github.com/libffi/libffi) (MIT license, ~50 KB compiled)
- **Embedded-friendly**: Static function table for FreeRTOS/bare-metal targets where `dlopen` is unavailable

## Architecture Overview

```
 Beerlang code                    Pure beerlang macros          VM / C layer
 ─────────────                    ──────────────────           ─────────────
 (sqrt 2.0)
     ↓
 (def-cfn sqrt ...)  ──expands──→  (defn sqrt [x]
                                    (ffi-call ptr
                                      [:double] :double
                                      [x]))
                                        ↓
                                   ffi-call native  ──→  libffi
                                        ↓                    ↓
                                   marshal Values      ffi_call()
                                   to/from C types     ABI handling
```

## VM Additions

### New Type: `TYPE_CPOINTER` (0x80)

Wraps a raw `void*`. Used for C function pointers, struct pointers, and opaque handles.

```c
typedef struct CPointer {
    struct Object header;
    void* ptr;
} CPointer;
```

Supports: null check (`cpointer?`, `cnull?`), equality (address comparison), printing (`#<cpointer 0x7fff...>`).

### New Natives

| Native | Signature | Description |
|---|---|---|
| `dlopen` | `(dlopen path)` → cpointer | Load shared library, returns handle |
| `dlsym` | `(dlsym handle name)` → cpointer | Look up symbol by name |
| `dlclose` | `(dlclose handle)` → nil | Unload shared library |
| `ffi-call` | `(ffi-call fn-ptr arg-types ret-type args)` → value | Call C function through libffi |
| `cmalloc` | `(cmalloc size-or-ctype)` → cpointer | Allocate C memory |
| `cfree` | `(cfree ptr)` → nil | Free C memory |
| `cget` | `(cget ptr type offset)` → value | Read C value at offset |
| `cset!` | `(cset! ptr type offset value)` → nil | Write C value at offset |
| `cpointer?` | `(cpointer? x)` → bool | Type predicate |
| `cnull?` | `(cnull? ptr)` → bool | Null pointer check |

### Static Function Table (Embedded)

For targets without `dlopen`, C code registers functions at startup:

```c
void ffi_register(const char* name, void* fn_ptr);
```

Beerlang looks up by name via `(ffi-lookup name)` instead of `dlsym`.

## Type Descriptors

Type descriptors are plain beerlang maps. The macro layer creates them; `ffi-call` consumes them.

### Primitive Types

Predefined in the `ffi` namespace:

```clojure
;; Each is a map like:
;; {:kind :primitive :name "int32" :size 4 :align 4 :ffi-type :sint32}

ffi/void ffi/bool
ffi/int8 ffi/uint8 ffi/int16 ffi/uint16
ffi/int32 ffi/uint32 ffi/int64 ffi/uint64
ffi/float ffi/double
ffi/pointer    ;; generic void*
ffi/string     ;; char* (auto marshal to/from beerlang string)
```

For convenience, keyword shorthands are accepted where type descriptors are expected:

```clojure
:void :bool :int8 :uint8 :int16 :uint16
:int32 :uint32 :int64 :uint64 :float :double
:pointer :string
```

### Struct Types

```clojure
(def-cstruct Point
  [x :double]
  [y :double])

;; Expands to:
;; (def Point {:kind :struct
;;             :name "Point"
;;             :size 16
;;             :align 8
;;             :fields [{:name :x :type :double :offset 0 :size 8}
;;                      {:name :y :type :double :offset 8 :size 8}]})
```

The macro computes sizes, alignment, and padding at macro-expansion time. Rules follow the C ABI:
- Each field aligned to its type's alignment
- Struct size padded to largest member alignment
- Nested structs use their own alignment

### Pointer Types

```clojure
(cpointer :int32)   ;; {:kind :pointer :to :int32 :size 8 :align 8}
(cpointer Point)    ;; {:kind :pointer :to Point  :size 8 :align 8}
```

### Array Types

```clojure
(carray :int32 10)  ;; {:kind :array :elem :int32 :count 10 :size 40 :align 4}
```

## Value Marshalling

### Beerlang → C

| Beerlang Type | C Type | Notes |
|---|---|---|
| fixnum | `int8..int64`, `uint8..uint64` | Range-checked, error on overflow |
| fixnum/bigint | `float`, `double` | Converted to floating point |
| `true`/`false` | `int` (1/0) | C convention |
| `nil` | `NULL` | For pointer types |
| string | `char*` | Null-terminated copy (pinned for call duration) |
| cpointer | `void*` | Direct pass-through |
| vector of values | struct | Packed into C struct layout per type descriptor |

### C → Beerlang

| C Type | Beerlang Type | Notes |
|---|---|---|
| `int8..int64` | fixnum (or bigint if overflow) | |
| `float`, `double` | fixnum (truncated) or future float type | |
| `int` used as bool | `true`/`false` | Only when return type declared `:bool` |
| `NULL` | `nil` | |
| `char*` | string | Copied into beerlang string |
| `void*` | cpointer | Wrapped |
| struct | vector or map | Unpacked per type descriptor |

### String Handling

Strings require care at the boundary:

```clojure
;; Beerlang strings are immutable, GC-managed.
;; For FFI calls, a temporary null-terminated copy is made.
;; The copy is freed after the call returns.
;; For functions that store the pointer, use (cstring str) to get
;; a manually-managed copy that must be freed with (cfree).
```

## Convenience Macros

### `def-cfn` — Bind a C Function

```clojure
(def-cfn sqrt "libm" "sqrt" [:double] :double)

;; Expands to:
;; (def __libm_handle (dlopen "libm"))
;; (def __sqrt_ptr (dlsym __libm_handle "sqrt"))
;; (defn sqrt [x]
;;   (ffi-call __sqrt_ptr [:double] :double [x]))
```

Library handles are cached — multiple `def-cfn` calls to the same library reuse one `dlopen`.

### `def-cfn` with Struct Arguments

```clojure
(def-cstruct Rect
  [x :int32] [y :int32]
  [w :int32] [h :int32])

(def-cfn draw-rect "libgfx" "draw_rect" [Rect] :void)

;; Struct values are passed as vectors:
(draw-rect [10 20 100 50])

;; Or as maps (resolved by field name):
(draw-rect {:x 10 :y 20 :w 100 :h 50})
```

### `with-lib` — Scoped Library Loading

```clojure
(with-lib [lib "libfoo"]
  (def-cfn foo lib "foo_init" [] :int32)
  (def-cfn bar lib "foo_bar" [:pointer :int32] :pointer))
;; Library handle closed when namespace is unloaded (future)
```

### Struct Access Helpers

```clojure
;; Allocate and work with C structs
(def p (cmalloc Point))
(cset! p Point :x 3.0)     ;; writes double at offset 0
(cset! p Point :y 4.0)     ;; writes double at offset 8
(cget p Point :x)           ;; reads double at offset 0 → 3.0

;; Macro sugar:
(def-cstruct-accessors Point)
;; Generates: point-x, point-y, set-point-x!, set-point-y!

(set-point-x! p 3.0)
(point-x p)                ;; → 3.0

(cfree p)
```

## The `ffi-call` Native — Implementation

This is the only complex C code in the FFI. Pseudocode:

```c
Value native_ffi_call(VM* vm, int argc, Value* argv) {
    // argv[0] = cpointer (function pointer)
    // argv[1] = vector of arg type descriptors (keywords or maps)
    // argv[2] = return type descriptor
    // argv[3] = vector of argument values

    void* fn_ptr = cpointer_get(argv[0]);

    // 1. Build ffi_type arrays from type descriptors
    int nargs = vector_length(argv[1]);
    ffi_type** arg_types = alloca(nargs * sizeof(ffi_type*));
    for (int i = 0; i < nargs; i++) {
        arg_types[i] = descriptor_to_ffi_type(vector_get(argv[1], i));
    }
    ffi_type* ret_type = descriptor_to_ffi_type(argv[2]);

    // 2. Prepare ffi_cif
    ffi_cif cif;
    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ret_type, arg_types);

    // 3. Marshal beerlang Values → C values
    void** arg_ptrs = alloca(nargs * sizeof(void*));
    for (int i = 0; i < nargs; i++) {
        arg_ptrs[i] = marshal_to_c(vector_get(argv[3], i),
                                   vector_get(argv[1], i));
    }

    // 4. Call
    uint8_t ret_buf[64];  // large enough for any return type
    ffi_call(&cif, fn_ptr, ret_buf, arg_ptrs);

    // 5. Marshal C result → beerlang Value
    return marshal_from_c(ret_buf, argv[2]);
}
```

### `descriptor_to_ffi_type` Mapping

```c
ffi_type* descriptor_to_ffi_type(Value desc) {
    // If keyword shorthand:
    if (is_keyword(desc)) {
        const char* name = keyword_name(desc);
        if (strcmp(name, "void") == 0)    return &ffi_type_void;
        if (strcmp(name, "int8") == 0)    return &ffi_type_sint8;
        if (strcmp(name, "uint8") == 0)   return &ffi_type_uint8;
        if (strcmp(name, "int32") == 0)   return &ffi_type_sint32;
        if (strcmp(name, "double") == 0)  return &ffi_type_double;
        if (strcmp(name, "pointer") == 0) return &ffi_type_pointer;
        if (strcmp(name, "string") == 0)  return &ffi_type_pointer;
        // ... etc
    }
    // If map with :kind :struct, build ffi_type_struct dynamically
    // (cache on the type descriptor map for reuse)
    // ...
}
```

## Embedded / FreeRTOS Integration

On targets without dynamic linking:

```c
// C startup code registers available functions
#include "beerlang_ffi.h"

void app_init(void) {
    ffi_register("gpio_set_level", (void*)gpio_set_level);
    ffi_register("adc_read",      (void*)adc_read);
    ffi_register("i2c_write",     (void*)i2c_write);
}
```

```clojure
;; Beerlang code — same type DSL, different lookup
(def gpio-set (ffi-lookup "gpio_set_level"))

(def-cfn-static gpio-set-level "gpio_set_level" [:uint32 :uint32] :void)
;; Uses ffi-lookup instead of dlsym, otherwise identical

(gpio-set-level 2 1)  ;; Set GPIO 2 high
```

libffi supports ARM Cortex-M and other embedded architectures, so `ffi-call` works unchanged.

## Callback Support (Future)

Allowing C code to call back into beerlang (e.g., for event handlers, comparators):

```clojure
;; Create a C-callable function pointer from a beerlang fn
(def my-callback (cfn [:int32 :int32] :int32
                   (fn [a b] (- a b))))

;; Pass to C function expecting a comparator
(qsort array count size my-callback)
```

This uses `ffi_closure_alloc` from libffi to create a native trampoline that calls back into the VM. This is more complex (needs to handle VM re-entry, possibly from a different thread) and is deferred to a later phase.

## Implementation Phases

### Phase 1: Core FFI (Minimal)

- `TYPE_CPOINTER` type
- `dlopen`, `dlsym`, `dlclose` natives
- `ffi-call` native with primitive type support
- `cmalloc`, `cfree`, `cget`, `cset!`
- `cpointer?`, `cnull?`
- Link libffi
- Keyword shorthands for primitive types

**Deliverable:** Call simple C functions (math, string ops) from the REPL.

### Phase 2: Type DSL

- `def-cstruct` macro with layout computation
- Struct marshalling (vector/map ↔ C struct)
- `def-cfn` convenience macro
- Library handle caching
- `def-cstruct-accessors` macro
- `ffi` namespace with predefined type descriptors

**Deliverable:** Work with C structs and libraries ergonomically.

### Phase 3: Embedded + Advanced

- Static function table (`ffi-register` / `ffi-lookup`)
- `def-cfn-static` macro
- Callback support (`cfn` / `ffi_closure`)
- Array types and bulk memory operations
- String pinning for long-lived C references

**Deliverable:** Full FFI for both desktop and embedded targets.

## Dependencies

- **libffi** (MIT license) — [github.com/libffi/libffi](https://github.com/libffi/libffi)
  - ~50 KB compiled
  - Supports: x86-64, ARM, AArch64, RISC-V, MIPS, PowerPC, and more
  - Available via system package managers (`apt install libffi-dev`, `brew install libffi`)
  - Can be vendored as a git submodule for embedded builds
- **`<dlfcn.h>`** (POSIX) — for `dlopen`/`dlsym`/`dlclose` on desktop targets
  - Not needed on embedded (static function table instead)

## Open Questions

1. **Float type in beerlang?** Currently beerlang has no native float type. FFI returning `double` would truncate to fixnum. Options: (a) add `TYPE_FLOAT` to the value system, (b) return as string representation, (c) return as a 2-element vector `[mantissa exponent]`. Adding a proper float type is probably the right call.

2. **Struct-by-value vs struct-by-pointer?** libffi supports both. By-pointer is simpler and more common in C APIs. By-value is needed for some APIs (e.g., returning small structs). Support both, default to by-pointer.

3. **Memory ownership conventions?** When C returns a `char*`, who owns it? Need conventions: `:copy` (beerlang copies and C retains ownership), `:own` (beerlang takes ownership and will `free`), `:borrow` (valid only during call, for callbacks).

4. **Thread safety?** If beerlang tasks call FFI functions that block, they block the whole scheduler. Options: (a) document that FFI calls should be fast, (b) dispatch long FFI calls on a separate OS thread and suspend the beerlang task until completion (matches the original design doc's "dispatched on separate threads" for shared libraries).

5. **Variadic C functions?** (`printf`, etc.) libffi supports variadic calls via `ffi_prep_cif_var`. Expose as a `:variadic` flag on `def-cfn`.
