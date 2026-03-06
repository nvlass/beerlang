# Beerlang Logging Guide

Beerlang uses the **microlog (ulog)** library for lightweight, efficient logging throughout the VM, compiler, and runtime.

## Quick Start

```c
#include "beerlang.h"

int main(void) {
    /* Initialize logging system */
    log_init();

    /* Your code here */
    LOG_INFO("VM started");
    LOG_DEBUG("Stack pointer: %d", vm->stack_pointer);

    /* ... */
}
```

## Log Levels

Beerlang provides six log levels in increasing severity:

| Level | Macro | Use For |
|-------|-------|---------|
| TRACE | `LOG_TRACE(...)` | Detailed execution traces (very verbose) |
| DEBUG | `LOG_DEBUG(...)` | Debug information for developers |
| INFO  | `LOG_INFO(...)`  | General informational messages |
| WARN  | `LOG_WARN(...)`  | Potential issues or warnings |
| ERROR | `LOG_ERROR(...)` | Error conditions |
| FATAL | `LOG_FATAL(...)` | Critical errors that may terminate |

### Default Levels

- **Release builds:** INFO and above
- **Debug builds:** DEBUG and above (compile with `-DDEBUG`)

## Usage Examples

### Basic Logging

```c
LOG_INFO("Beerlang VM initialized successfully");
LOG_DEBUG("Loaded %d functions", function_count);
LOG_WARN("Stack usage: %d/%d (%.1f%%)", used, total, percent);
LOG_ERROR("Invalid opcode: 0x%02x at pc=%d", opcode, pc);
```

### Topic-Based Logging

For subsystem-specific logging:

```c
LOG_VM_DEBUG("CALL: n_args=%d, sp=%d, pc=%d", n_args, sp, pc);
LOG_MEM_TRACE("Allocated %zu bytes for type %d", size, type);
LOG_COMP_DEBUG("Compiling function: %s", fn_name);
```

Available topics:
- `LOG_VM_TRACE/DEBUG` - Virtual machine operations
- `LOG_MEM_TRACE/DEBUG` - Memory management
- `LOG_COMP_TRACE/DEBUG` - Compiler operations

## Configuration

### Change Log Level at Runtime

```c
log_set_level(ULOG_LEVEL_TRACE);  /* Very verbose */
log_set_level(ULOG_LEVEL_ERROR);  /* Quiet, errors only */
```

### Log to File

```c
log_add_file("beerlang.log", ULOG_LEVEL_DEBUG);
```

This will log all DEBUG and above messages to `beerlang.log` in addition to stdout.

### Disable Logging Completely

Compile with `-DLOG_DISABLED` to remove all logging overhead:

```bash
make CFLAGS="-DLOG_DISABLED"
```

All LOG_* macros become no-ops with zero runtime cost.

## VM Opcode Tracing

The VM includes comprehensive TRACE-level logging for every bytecode instruction executed. This is invaluable for debugging bytecode generation, offset calculations, and understanding VM behavior.

### What Gets Logged

Every opcode execution logs:
- **PC** - Program counter (bytecode address) before instruction
- **Opcode** - Hex value of the opcode byte
- **Name** - Human-readable opcode name
- **Operands** - Instruction arguments (immediates, offsets, indices)
- **Stack pointer** - Current stack depth

### Example Output

```
TRACE src/vm/vm.c:326: PC=0000 OP=14 (OP_PUSH_INT) value=42 sp=0
TRACE src/vm/vm.c:335: PC=0009 OP=13 (OP_PUSH_CONST) idx=0 sp=1
TRACE src/vm/vm.c:686: PC=0014 OP=63 (OP_CALL) n_args=2 target=100 sp=3
TRACE src/vm/vm.c:653: PC=0100 OP=67 (OP_ENTER) n_locals=2 sp=2
TRACE src/vm/vm.c:829: PC=0103 OP=22 (OP_LOAD_LOCAL) idx=0 sp=4
TRACE src/vm/vm.c:349: PC=0106 OP=30 (OP_ADD) sp=5
TRACE src/vm/vm.c:798: PC=0107 OP=65 (OP_RETURN) return_pc=17 frame_count=1 sp=4
TRACE src/vm/vm.c:966: PC=0017 OP=6F (OP_HALT) sp=1
```

### Enabling Opcode Tracing

**In tests:**
```c
int main(void) {
    log_init();
    log_set_level(ULOG_LEVEL_TRACE);  /* Enable all traces */

    /* Run VM */
    vm_run(vm);
}
```

**Debug builds (default TRACE enabled):**
```bash
make debug
./bin/beerlang
```

**Release builds (INFO by default):**
```bash
make
./bin/beerlang  # No traces

# Or enable traces at runtime:
log_set_level(ULOG_LEVEL_TRACE);
```

### Use Cases

1. **Debugging bytecode offsets:**
   - Track exact PC values to verify jump targets
   - See where function calls land
   - Verify return addresses

2. **Understanding execution flow:**
   - Follow control flow (jumps, calls, returns)
   - Watch stack grow/shrink
   - Identify infinite loops

3. **Validating bytecode generation:**
   - Compare expected vs actual instruction sequence
   - Verify operand values
   - Check stack balance

4. **Performance profiling:**
   - Count instruction execution
   - Identify hot loops
   - Measure call depth

### Example: Debugging Offset Issues

```c
/* Test with known bytecode */
uint8_t code[] = {
    OP_ENTER, 0, 2,          /* PC=0: ENTER n_locals=2 */
    OP_LOAD_LOCAL, 0, 0,     /* PC=3: LOAD_LOCAL idx=0 */
    OP_LOAD_LOCAL, 1, 0,     /* PC=6: LOAD_LOCAL idx=1 */
    OP_ADD,                  /* PC=9: ADD */
    OP_RETURN,               /* PC=10: RETURN */
    /* main starts at PC=11 */
    OP_PUSH_INT, ...         /* PC=11: ... */
};

vm_load_code(vm, code, sizeof(code));
vm->pc = 11;  /* Start at main */

log_set_level(ULOG_LEVEL_TRACE);
vm_run(vm);

/* Output will show exact PC progression */
```

### Performance Impact

- **With `-DLOG_DISABLED`**: Zero overhead (traces compiled out)
- **TRACE disabled at runtime**: ~1 comparison per instruction
- **TRACE enabled**: String formatting overhead (debug only)

**Recommendation:** Use TRACE freely during development, compile with `-DLOG_DISABLED` for production.

## Integration in Your Code

### VM Operations

```c
void vm_step(VM* vm) {
    LOG_VM_TRACE("Executing opcode 0x%02x at pc=%d", opcode, vm->pc);

    switch (opcode) {
        case OP_ADD:
            LOG_VM_DEBUG("ADD: a=%d, b=%d", a, b);
            /* ... */
            break;
    }
}
```

### Memory Management

```c
void* object_alloc(size_t size, uint8_t type) {
    LOG_MEM_TRACE("Allocating %zu bytes for type %d", size, type);
    void* ptr = malloc(size);

    if (!ptr) {
        LOG_ERROR("Allocation failed: %zu bytes", size);
        return NULL;
    }

    LOG_MEM_DEBUG("Allocated at %p", ptr);
    return ptr;
}
```

### Error Reporting

```c
if (vm->stack_pointer < 2) {
    LOG_ERROR("Stack underflow: sp=%d, required=2", vm->stack_pointer);
    vm_error(vm, "Stack underflow");
    return;
}
```

## Best Practices

1. **Use appropriate levels:**
   - TRACE for very detailed flow (hot loops, every instruction)
   - DEBUG for development info (function calls, state changes)
   - INFO for user-facing messages (initialization, major events)
   - WARN for recoverable issues
   - ERROR for failures
   - FATAL for unrecoverable errors

2. **Keep overhead low:**
   - Use DEBUG/TRACE for performance-critical paths
   - They're compiled out in release builds (with `-O2`)

3. **Include context:**
   ```c
   /* Good */
   LOG_ERROR("Division by zero at pc=%d", vm->pc);

   /* Less useful */
   LOG_ERROR("Division by zero");
   ```

4. **Topic-based for subsystems:**
   - Use `LOG_VM_*` for VM-specific logs
   - Use `LOG_MEM_*` for memory operations
   - Use `LOG_COMP_*` for compiler operations

## Library Details

Beerlang uses **microlog (ulog) v7.0.0**:
- **License:** MIT
- **Source:** https://github.com/an-dr/microlog
- **Location:** `lib/ulog.h`, `lib/ulog.c`

See `THIRD_PARTY_LICENSES.md` for full license information.

## Advanced Features

For advanced usage (custom handlers, dynamic configuration), see the microlog documentation at: https://github.com/an-dr/microlog

Note: Some advanced features require compile-time flags like `-DULOG_BUILD_DYNAMIC_CONFIG=1`.
