## Instruction Set

Beerlang uses a stack-based VM with single-byte opcodes and variable-length operands. This document reflects the **currently implemented** instruction set, with future/reserved opcodes listed separately at the end.

### Operand Notation

| Notation | Size | Description |
|----------|------|-------------|
| `u16` | 2 bytes | Unsigned 16-bit integer (little-endian) |
| `u32` | 4 bytes | Unsigned 32-bit integer (little-endian) |
| `i32` | 4 bytes | Signed 32-bit integer (little-endian, relative offset) |
| `i64` | 8 bytes | Signed 64-bit integer (little-endian) |

---

### Implemented Opcodes

#### Stack Operations (0x00 - 0x0F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x00 | `NOP` | — | No operation |
| 0x01 | `POP` | — | Pop top of stack |
| 0x02 | `DUP` | — | Duplicate top of stack |
| 0x03 | `SWAP` | — | Swap top two stack elements |
| 0x04 | `OVER` | — | Copy second element to top |

#### Constants & Literals (0x10 - 0x1F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x10 | `PUSH_NIL` | — | Push nil |
| 0x11 | `PUSH_TRUE` | — | Push true |
| 0x12 | `PUSH_FALSE` | — | Push false |
| 0x13 | `PUSH_CONST` | `u32` index | Push value from constant pool |
| 0x14 | `PUSH_INT` | `i64` value | Push 64-bit integer literal |

#### Variables & Scope (0x20 - 0x2F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x20 | `LOAD_VAR` | `u16` const_idx | Load value from namespace var (symbol at constant pool index) |
| 0x21 | `STORE_VAR` | `u16` const_idx | Store top-of-stack to namespace var (symbol at constant pool index) |
| 0x22 | `LOAD_LOCAL` | `u16` slot | Load from local variable slot |
| 0x23 | `STORE_LOCAL` | `u16` slot | Store to local variable slot (retains new before releasing old) |
| 0x24 | `LOAD_CLOSURE` | `u16` index | Load from closure's captured environment |
| 0x25 | `LOAD_SELF` | — | Push the currently executing function onto the stack (for named fn self-recursion) |

#### Arithmetic Operations (0x30 - 0x3F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x30 | `ADD` | — | Pop two, push sum |
| 0x31 | `SUB` | — | Pop two, push difference (second - top) |
| 0x32 | `MUL` | — | Pop two, push product |
| 0x33 | `DIV` | — | Pop two, push quotient (second / top) |
| 0x35 | `NEG` | — | Negate top of stack |
| 0x36 | `INC` | — | Increment top by 1 |
| 0x37 | `DEC` | — | Decrement top by 1 |

Note: `mod` and `rem` are implemented as native functions, not opcodes. Slot 0x34 is unused.

#### Comparison Operations (0x40 - 0x4F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x40 | `EQ` | — | Pop two, push true if equal (cross-type structural equality) |
| 0x42 | `LT` | — | Pop two, push true if second < top |
| 0x44 | `GT` | — | Pop two, push true if second > top |

Note: `>=` and `<=` are implemented as native functions, not opcodes. `NEQ` (0x41), `LTE` (0x43), `GTE` (0x45) are not implemented.

#### Control Flow (0x60 - 0x6F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x60 | `JUMP` | `i32` offset | Unconditional relative jump |
| 0x61 | `JUMP_IF_FALSE` | `i32` offset | Jump if top is false or nil (peeks, does not pop) |
| 0x63 | `CALL` | `u16` n_args | Call function with n_args arguments |
| 0x64 | `TAIL_CALL` | `u16` n_args | Tail call — reuses current frame (compiler-generated) |
| 0x65 | `RETURN` | — | Return from function (retains return value before frame cleanup) |
| 0x67 | `ENTER` | `u16` n_locals | Function prologue — allocate n_locals local variable slots |
| 0x6F | `HALT` | — | Stop VM execution |

Note: `JUMP_IF_FALSE` peeks at the stack (does not consume the value). The compiler emits a `POP` after the jump when needed.

#### Exception Handling (0x70 - 0x7F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x70 | `PUSH_HANDLER` | `u32` catch_pc | Push an exception handler; catch_pc is the absolute PC of the catch block |
| 0x71 | `POP_HANDLER` | — | Remove the top exception handler (normal exit from try block) |
| 0x72 | `THROW` | — | Pop a value (must be a hashmap) and throw it as an exception |
| 0x73 | `LOAD_EXCEPTION` | — | Push the current exception (`vm->exception`) onto the stack |

#### Functions & Closures (0x80 - 0x8F)

| Opcode | Instruction | Operands | Description |
|--------|-------------|----------|-------------|
| 0x80 | `MAKE_CLOSURE` | `u32` code_offset, `u16` n_locals, `u16` n_closed, `u16` arity, `u16` name_idx | Create a closure object. `code_offset` is the byte offset into the code buffer. `n_closed` captured values are popped from the stack. `name_idx` is a constant pool index for the function name (0xFFFF = anonymous). |

---

### Future / Reserved Opcodes

The following opcodes are **not implemented**. They are listed here as design placeholders for future development. The VM will trap (error) on any unrecognized opcode.

#### Logical & Bitwise Operations (0x50 - 0x5F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x50 | `NOT` | Logical not |
| 0x51 | `BIT_AND` | Bitwise and |
| 0x52 | `BIT_OR` | Bitwise or |
| 0x53 | `BIT_XOR` | Bitwise xor |
| 0x54 | `BIT_NOT` | Bitwise not |
| 0x55 | `SHL` | Shift left |
| 0x56 | `SHR` | Shift right |

#### Control Flow — Reserved Slots

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x62 | `JUMP_IF_TRUE` | Jump if top is truthy |
| 0x66 | `YIELD` | Cooperative yield (for multitasking scheduler) |

#### Data Structure Operations (0x70 range, currently occupied by exception handling)

These were originally planned in the 0x70 range but that range is now used for exception handling. If implemented, they would be relocated:

| Instruction | Description |
|-------------|-------------|
| `MAKE_LIST <n>` | Create list from top n items |
| `MAKE_VECTOR <n>` | Create vector from top n items |
| `MAKE_MAP <n>` | Create map from top 2n items |
| `GET` | Get element from collection |
| `ASSOC` | Associate key with value |
| `CONJ` | Add to collection |
| `FIRST` | Get first element |
| `REST` | Get rest of collection |
| `COUNT` | Get collection size |
| `NTH` | Get nth element |

Note: All of these are currently available as native functions. They would only become opcodes if profiling reveals a performance need.

#### Functions & Closures — Reserved Slots

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x81 | `APPLY <n>` | Apply function to argument list |
| 0x82 | `PARTIAL <n>` | Partial function application |

#### FFI & Native Calls (0xA0 - 0xAF)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0xA0 | `LOAD_LIB <idx>` | Load shared library |
| 0xA1 | `CALL_NATIVE <idx> <n>` | Call native function with n args |

#### Type Checking & Metadata (0xB0 - 0xBF)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0xB0 | `TYPE_OF` | Push type tag of top-of-stack |
| 0xB1 | `META` | Get metadata |
| 0xB2 | `WITH_META` | Attach metadata |

**Rationale**: Keep VM minimal and cache-efficient. Add instructions only when measurements prove they are necessary. Old bytecode remains compatible forever.
