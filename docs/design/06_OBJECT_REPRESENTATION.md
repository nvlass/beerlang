### Object Representation, Data Structures and Memory Layout

#### Tagged Union Value Struct

All values in Beerlang are represented as 16-byte tagged union structs (similar to Lua's TValue). The tag is an explicit `ValueTag` enum field, and the payload is a union of immediate types and a heap object pointer.

```c
typedef enum {
    TAG_NIL = 0,    // nil
    TAG_TRUE,       // true
    TAG_FALSE,      // false
    TAG_FIXNUM,     // 64-bit signed integer (immediate)
    TAG_CHAR,       // Unicode codepoint (immediate)
    TAG_OBJECT,     // Pointer to heap-allocated Object
} ValueTag;

typedef struct Value {
    ValueTag tag;
    union {
        int64_t        fixnum;     /* TAG_FIXNUM */
        uint32_t       character;  /* TAG_CHAR */
        struct Object* object;     /* TAG_OBJECT - all heap types */
    } as;
} Value;
```

**Pre-defined constants:**
```c
static const Value VALUE_NIL   = { .tag = TAG_NIL,   .as = { .fixnum = 0 } };
static const Value VALUE_TRUE  = { .tag = TAG_TRUE,  .as = { .fixnum = 0 } };
static const Value VALUE_FALSE = { .tag = TAG_FALSE, .as = { .fixnum = 0 } };
```

Values are compared with `value_identical()`, which checks tag equality first, then compares the appropriate union field.

#### Immediate Values (No Allocation)

**Fixnum (TAG_FIXNUM):**
- Full `int64_t` signed integer stored directly in the union
- Range: -2^63 to 2^63-1
- No allocation, no refcounting needed
- Arithmetic may overflow to bigint

**Character (TAG_CHAR):**
- Unicode codepoint (`uint32_t`)
- Stored directly in the union
- No allocation needed

**Special Constants (TAG_NIL, TAG_TRUE, TAG_FALSE):**
- `nil`, `true`, `false` each have a dedicated tag value
- No allocation, recognized by tag alone
- The union payload is unused (zeroed)

#### Heap-Allocated Objects

All heap objects have a common header followed by type-specific data:

```c
struct Object {
    uint32_t type;      // Object type (8 bits type + 24 bits flags)
    uint32_t refcount;  // Reference count
    uint32_t size;      // Size/length (type-dependent meaning)
    void*    meta;      // Metadata pointer (or NULL)
};
```

**Object Header (16 bytes on 64-bit):**
- `type` (4 bytes): 8-bit type code + 24 bits for flags/subtype
- `refcount` (4 bytes): Reference count (32-bit, sufficient for practical use)
- `size` (4 bytes): Type-dependent (length, capacity, hash, etc.)
- `meta` (8 bytes): Optional metadata pointer (or NULL)

#### Object Type Codes

```
0x01 - Bigint       (arbitrary precision integer)
0x02 - Float        (64-bit IEEE 754)
0x08 - Namespace    (collection of vars)
0x10 - Symbol       (interned identifier)
0x11 - Keyword      (interned keyword)
0x12 - String       (UTF-8 string)
0x20 - Cons         (cons cell / list node)
0x21 - Vector       (dynamic array)
0x22 - HashMap      (hash table)
0x30 - Function     (bytecode function / closure)
0x31 - NativeFunction (C function callable from Beerlang)
0x40 - Var          (namespace-level binding)
0x41 - Namespace    (reserved alias)
```

#### Object Types

**Bigint (type 0x01):**
```c
struct Bigint {
    Object   header;
    mpz_t    value;     // GMP arbitrary precision integer
};
```

**Float (type 0x02):**
```c
struct Float {
    Object   header;
    double   value;     // 64-bit IEEE 754
};
```

**Namespace (type 0x08):**
```c
struct Namespace {
    struct Object header;    /* Object header (type = TYPE_NAMESPACE) */
    const char* name;        /* Namespace name (owned string) */
    Value vars;              /* HashMap: Symbol -> Var (as tagged pointer) */
};
```
- First-class namespace objects
- Stored in global namespace registry (singleton pattern)
- Each namespace owns its vars
- Supports REPL introspection and dynamic loading

**Symbol (type 0x10):**
```c
struct Symbol {
    Object      header;    // size = hash code
    Namespace*  ns;        // Namespace (or NULL if unqualified)
    char        name[];    // UTF-8 string: simple name only (e.g., "foo")
};
```
- Symbols are interned (one instance per unique namespace+name combination)
- Namespace stored as pointer to singleton Namespace object
- Unqualified symbols have `ns == NULL`
- Name is simple (not "namespace/name", just "foo")
- Memory efficient: many symbols share same namespace pointer

**Keyword (type 0x11):**
```c
struct Keyword {
    Object      header;    // size = hash code
    Namespace*  ns;        // Namespace (or NULL if unqualified)
    char        name[];    // UTF-8 string: simple name only (e.g., "foo")
};
```
- Similar to symbols but distinct type
- Always interned
- Used as map keys and enums
- Share namespace pointers with symbols

**String (type 0x12):**
```c
struct String {
    Object   header;    // size = byte length
    uint32_t char_len;  // Character length (UTF-8)
    char     data[];    // UTF-8 encoded string data
};
```
- Immutable
- Both byte length and character count stored for efficiency
- Always NUL-terminated for C interop

**Cons Cell / List Node (type 0x20):**
```c
typedef struct {
    struct Object header;
    Value car;  /* First element */
    Value cdr;  /* Rest of list */
} Cons;
```
- Classic cons cell for linked lists
- Immutable, structural sharing
- `cdr` must be either another Cons or nil

**Vector (type 0x21):**
```c
typedef struct {
    struct Object header;
    size_t length;      /* Number of elements */
    size_t capacity;    /* Allocated capacity */
    Value* elements;    /* Dynamic array of values */
} Vector;
```
- Simple dynamic array with amortized O(1) append
- Default initial capacity of 8 elements
- Grows by doubling when full
- O(1) indexed access and O(n) update

> **Future: Persistent Implementation** -- The current mutable dynamic array will be replaced with a persistent vector using HAMT/RRB-tree structure for O(log32 N) access and update with structural sharing.

**HashMap (type 0x22):**
```c
typedef struct {
    Value key;
    Value value;
    bool occupied;
    bool deleted;  /* Tombstone marker */
} HashEntry;

typedef struct {
    struct Object header;
    size_t size;         /* Number of key-value pairs */
    size_t capacity;     /* Size of entries array */
    size_t tombstones;   /* Number of deleted entries */
    HashEntry* entries;  /* Hash table */
} HashMap;
```
- Open-addressing hash table with linear probing
- Default initial capacity of 16 entries
- Resizes at 75% load factor
- Tombstone-based deletion
- Keys compared by value equality (`value_equal`)

> **Future: Persistent Implementation** -- The current mutable hash table will be replaced with a persistent HAMT (Hash Array Mapped Trie) for O(log32 N) lookup and update with structural sharing.

**Function (type 0x30):**
```c
typedef struct Function {
    struct Object header;    /* header.size = arity (-1 for variadic) */
    uint32_t code_offset;    /* Offset in bytecode where function starts */
    uint16_t n_locals;       /* Number of local variable slots */
    uint16_t n_closed;       /* Number of closed-over values */
    uint8_t* code;           /* Pointer to bytecode array */
    int code_size;           /* Size of bytecode array */
    Value* constants;        /* Pointer to constants array */
    int num_constants;       /* Number of constants */
    char* name;              /* Function name (owned, always non-NULL) */
    Value    closed[];       /* Flexible array: closure environment */
} Function;
```
- Bytecode functions with optional closure
- Self-contained execution context (code + constants pointers set after compilation)
- `closed[]` captures lexical environment via flexible array member
- Arity stored in header for dispatch optimization

**Native Function (type 0x31):**
```c
typedef struct NativeFunction {
    struct Object header;    /* header.size = arity (-1 for variadic) */
    NativeFn fn_ptr;        /* C function pointer */
    const char* name;       /* Function name (for debugging/errors) */
} NativeFunction;
```
- C functions callable from Beerlang
- Must follow calling convention (stack-based)

**Var (type 0x40):**
```c
struct Var {
    struct Object header;  /* Object header (type = TYPE_VAR) */
    Value name;      /* Symbol */
    Value value;     /* Current value */
    bool is_macro;   /* Is this a macro? */
    Value meta;      /* Metadata (map) */
};
```
- Namespace-level named values
- Can be rebound (dynamic vars)
- Macro flag for compile-time expansion
- Owned by the Namespace (stored in Namespace.vars map)

#### Namespace Registry and Symbol Resolution

**Global Namespace Registry:**
The VM maintains a global registry of all namespaces:

```c
struct NamespaceRegistry {
    Value namespaces;      /* HashMap: String -> Namespace* (as pointer value) */
    Namespace* current;    /* Current namespace (for REPL) */
};
```

**Namespace as Singletons:**
- Only one Namespace object exists per namespace name
- When resolving `user/foo`, the same "user" Namespace is returned every time
- All symbols in "user" share the same `Namespace*` pointer
- Memory efficient: "user" stored once, not duplicated in every symbol

**Symbol Interning:**
Symbols and keywords are interned in global intern tables:

```c
// Global VM state
HashMap* symbol_intern_table;   // Map: (Namespace*, name) -> Symbol
HashMap* keyword_intern_table;  // Map: (Namespace*, name) -> Keyword
```

**Lookup Process:**

1. **Creating/resolving a namespace:**
   ```clojure
   (ns user)  ; or referencing user/foo
   ```
   - Lookup "user" in namespace_registry
   - If not found, create new Namespace object and register it
   - Return singleton Namespace*

2. **Creating/resolving a symbol:**
   ```clojure
   'user/foo
   ```
   - Resolve namespace "user" (get Namespace*)
   - Lookup (namespace_ptr, "foo") in symbol_intern_table
   - If not found, create new Symbol with ns=namespace_ptr, name="foo"
   - Return interned Symbol*

3. **Defining a var:**
   ```clojure
   (def foo 42)
   ```
   - Get current namespace (e.g., "user")
   - Create symbol `user/foo` (interned)
   - Create Var with that symbol
   - Store in current_namespace->vars["foo"] = var
   - Store in global var index for fast LOAD_VAR access

4. **Resolving a var at compile-time:**
   ```clojure
   foo        ; or user/foo
   ```
   - Parse namespace (default to current ns if unqualified)
   - Lookup namespace in registry -> Namespace*
   - Lookup simple name in Namespace->vars -> Var*
   - Get Var's index in global var table
   - Emit `LOAD_VAR <idx>` bytecode

5. **Runtime var access:**
   ```
   LOAD_VAR <idx>  ; Direct array index, O(1)
   ```
   - No hash lookups at runtime!
   - All resolution happens at compile-time

**Benefits:**
- **Memory efficient**: Namespace name stored once, shared by all symbols
- **REPL friendly**: Can enumerate namespaces, list vars, introspection
- **Compile-time resolution**: Double hash lookup happens once, compiled to direct index
- **Dynamic loading**: Can create/remove namespaces at runtime
- **Namespace operations**: `all-ns`, `ns-publics`, `ns-interns`, `require`, `use`
- **Tooling support**: IDEs can discover all symbols in a namespace

**Example Memory Layout:**

```
Namespace Registry:
  "user" -> Namespace{name="user", vars={...}}
  "clojure.core" -> Namespace{name="clojure.core", vars={...}}

Symbol Intern Table:
  (Namespace["user"], "foo") -> Symbol{ns=Namespace["user"], name="foo"}
  (Namespace["user"], "bar") -> Symbol{ns=Namespace["user"], name="bar"}
  (NULL, "foo") -> Symbol{ns=NULL, name="foo"}  // unqualified

Namespace["user"].vars:
  "foo" -> Var{name=Symbol[user/foo], value=42}
  "bar" -> Var{name=Symbol[user/bar], value=#<Fn>}
```

Notice how many symbols can share the same `Namespace*` pointer, eliminating string duplication.

#### Future: Persistent Data Structure Implementation

**Vectors and HashMaps will eventually use structural sharing:**

```
Original vector: [a b c d e]
Updated vector:  [a b X d e]
                    └─┬─┘
                  shared nodes
```

- Only modified path is copied
- O(log32 N) time and space
- Old version remains valid
- Fits perfectly with reference counting (tree-shaped graphs)

**HAMT (Hash Array Mapped Trie):**
- 32-way branching at each level
- Bitmap compression (sparse arrays)
- Excellent cache locality
- Will be used for both vectors and hashmaps

The current implementation uses mutable dynamic arrays (Vector) and open-addressing hash tables (HashMap) for simplicity. Persistent implementations are planned for a future phase.

#### Memory Layout Considerations

**Alignment:**
- All heap objects 8-byte aligned
- Header size: 16 bytes (good cache line utilization)
- Small objects (cons, closures) fit in single cache line (64 bytes)

**Value Size:**
- Each `Value` is 16 bytes (tag + union)
- Immediate values (fixnum, char, nil, true, false) require no heap allocation
- Heap object pointer stored directly in the union

**Cache Efficiency:**
- Small objects stay compact
- Reference counting keeps working set small
- Future persistent structures will maximize sharing (less memory pressure)

**Size Overhead:**
- 16-byte header per heap object (acceptable for structures that share data)
- Immediate values (fixnum, char, bool, nil) have zero heap overhead (16 bytes on the stack/in collections)
- Most programs dominated by collections with good data-to-header ratio
