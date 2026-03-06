# Beerlang Memory Model

## Value Representation

All values in Beerlang are represented as 16-byte tagged union structs:

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
        int64_t        fixnum;     // TAG_FIXNUM
        uint32_t       character;  // TAG_CHAR
        struct Object* object;     // TAG_OBJECT - all heap types
    } as;
} Value;
```

### Immediate Values (No Heap Allocation)

These values are stored **directly in the Value struct**, requiring no memory management:

| Type | Tag | Storage | Memory Management |
|------|-----|---------|-------------------|
| nil | `TAG_NIL` | No payload | **None needed** |
| true | `TAG_TRUE` | No payload | **None needed** |
| false | `TAG_FALSE` | No payload | **None needed** |
| Fixnum | `TAG_FIXNUM` | Full int64_t in union | **None needed** |
| Character | `TAG_CHAR` | uint32_t Unicode codepoint | **None needed** |

**Example:**
```c
Value v = make_fixnum(42);  // No allocation, just struct initialization
// v.tag = TAG_FIXNUM, v.as.fixnum = 42
// No need to release or free
```

### Heap-Allocated Objects (Require Memory Management)

These values have `tag = TAG_OBJECT` and point to heap objects:

| Type | Type Code | Storage | Memory Management |
|------|-----------|---------|-------------------|
| Bigint | `0x01` | Object + mpz_t | **Refcounted** - call `value_release()` |
| Float | `0x02` | Object + double | **Refcounted** - call `value_release()` |
| String | `0x12` | Object + UTF-8 data | **Refcounted** - call `value_release()` |
| Symbol | `0x10` | Object + name data | **Interned** - never release |
| Keyword | `0x11` | Object + name data | **Interned** - never release |
| Cons | `0x20` | Object + car/cdr Values | **Refcounted** - call `value_release()` |
| Vector | `0x21` | Object + elements array | **Refcounted** - call `value_release()` |
| HashMap | `0x22` | Object + hash table | **Refcounted** - call `value_release()` |
| Function | `0x30` | Object + bytecode refs + closed[] | **Refcounted** - call `value_release()` |
| NativeFunction | `0x31` | Object + C fn pointer | **Refcounted** - call `value_release()` |
| Var | `0x40` | Object + name + value | **Refcounted** - call `value_release()` |
| Namespace | `0x08` | Object + name + vars map | **Refcounted** - call `value_release()` |

**Example:**
```c
Value str = string_from_cstr("hello");  // Allocates on heap, refcount = 1
// str.tag = TAG_OBJECT, str.as.object -> String on heap
value_release(str);  // Decrement refcount, free when it reaches 0
```

## Object Header

All heap-allocated objects start with a 16-byte header:

```c
struct Object {
    uint32_t type;      // Object type (8 bits) + flags (24 bits)
    uint32_t refcount;  // Reference count (starts at 1)
    uint32_t size;      // Type-dependent (length, capacity, hash, etc.)
    void*    meta;      // Metadata pointer (or NULL)
};
```

## Reference Counting Rules

### For Regular Heap Objects

1. **Creation**: Object starts with `refcount = 1`
   ```c
   Value obj = bigint_from_int64(1000);  // refcount = 1
   ```

2. **Sharing**: Call `object_retain()` when storing in another structure
   ```c
   object_retain(obj.as.object);  // refcount = 2
   ```

3. **Release**: Call `object_release()` (via `value_release()`) when done
   ```c
   value_release(obj);  // refcount--, free when it reaches 0
   ```

### For Interned Objects (Symbol, Keyword)

**Do NOT call `object_release()` on symbols/keywords!**

- Interned values are created once and live until program shutdown
- Same name always returns same pointer
- Their refcount is managed by the intern table, not by callers

```c
Value sym1 = symbol_intern("foo", NULL);  // Creates and interns
Value sym2 = symbol_intern("foo", NULL);  // Returns same object
// sym1.as.object == sym2.as.object  (pointer equality)

// Do NOT release:
// value_release(sym1);  // WRONG! Never do this
```

### For Immediate Values (Fixnum, Character, nil, true, false)

**No memory management needed at all!**

```c
Value num = make_fixnum(42);
Value chr = make_char('A');
Value nil = VALUE_NIL;

// No retain/release needed
// No allocation/deallocation
// Just use the values
```

## VM Stack Reference Counting

The VM stack **does** perform reference counting:

- **`vm_push()`** retains the value (refcount++)
- **`vm_pop()`** releases the value (refcount--)

### Critical Ordering Rules

**STORE_LOCAL**: Retain the new value BEFORE releasing the old:
```c
// CORRECT:
value_retain(new_val);       // new_val might be sub-object of old
value_release(old_val);      // safe to release now
locals[slot] = new_val;

// WRONG:
value_release(old_val);      // might free new_val if it's a sub-object!
value_retain(new_val);       // too late - may be dangling
```

**RETURN**: Retain the return value before cleaning up locals:
```c
Value ret = vm_pop(vm);      // pop return value
value_retain(ret);           // protect it
// ... release all locals and args in cleanup loop ...
vm_push(vm, ret);            // push to caller's stack
value_release(ret);          // balance the extra retain
```

## Memory Management Helpers

```c
// value_release handles the tag check internally:
void value_release(Value v) {
    if (v.tag != TAG_OBJECT) return;        // immediate - nothing to do
    Object* obj = v.as.object;
    uint8_t type = obj->type & 0xFF;
    if (type == TYPE_SYMBOL || type == TYPE_KEYWORD) return;  // interned
    object_release(obj);                     // decrement refcount
}
```

## Common Patterns

### Returning a New Object
```c
Value create_something(void) {
    Object* obj = object_alloc(TYPE_SOMETHING, size);  // refcount = 1
    // ... initialize ...
    return (Value){ .tag = TAG_OBJECT, .as.object = obj };  // Caller owns reference
}
```

### Storing in a Structure
```c
void set_field(MyStruct* s, Value new_val) {
    value_retain(new_val);       // Retain new FIRST
    value_release(s->field);     // Release old SECOND
    s->field = new_val;
}
```

### Temporary Values
```c
void example(void) {
    Value temp = string_from_cstr("temporary");  // refcount = 1
    // Use temp...
    value_release(temp);  // Free when done
}
```

### Temp VM Pattern (calling bytecode from C)
Used by `expand_macro`, `apply`, `macroexpand-1`:
```c
// 1. Build mini bytecode: PUSH_CONST args, PUSH_CONST fn, CALL n, HALT
// 2. Run in a temporary VM
// 3. Retain result BEFORE freeing the temp VM
// 4. Free the temp VM
// 5. Return the retained result
```

## Why Fixnums Don't Need GC

Fixnums are stored directly in the Value struct's union — there's no heap pointer:

```
Value for fixnum 42:
  tag = TAG_FIXNUM (3)
  as.fixnum = 42

Value for string "hello":
  tag = TAG_OBJECT (5)
  as.object = 0x7f8a12345678  →  Object header + "hello" bytes on heap
```

Fixnums are just integers in a struct field — there's no allocation, so there's nothing to free!

## Leak Tracking

Build with `make track-leaks`, then run with `--dump-leaks` to see unreleased objects at shutdown. Expected leaks include CompiledCode objects kept alive by the REPL.

## See Also

- `docs/05_GARBAGE_COLLECTION.md` - GC strategy and cycle detection plans
- `docs/06_OBJECT_REPRESENTATION.md` - Detailed type layouts
- `docs/07_CALL_CONVENTIONS_AND_FFI.md` - Stack refcounting in CALL/RETURN
- `include/value.h` - Value representation and tag definitions
