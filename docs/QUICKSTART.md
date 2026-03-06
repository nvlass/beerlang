# Beerlang Quick Start Guide

## Setup

1. **Clone and enter directory:**
   ```bash
   cd beerlang
   ```

2. **Build the project:**
   ```bash
   make
   ```

3. **Run tests:**
   ```bash
   make test
   ```

## Testing Framework

Beerlang uses a simple, minunit-style testing framework defined in `include/test.h`.

### Writing Tests

```c
#include "test.h"
#include "value.h"

/* Define a test */
TEST(my_test) {
    Value v = make_fixnum(42);
    ASSERT(is_fixnum(v), "Should be fixnum");
    ASSERT_EQ(untag_fixnum(v), 42, "Should be 42");
    return NULL;  /* NULL means success */
}

/* Test suite */
static const char* all_tests() {
    RUN_TEST(my_test);
    return NULL;
}

/* Main */
int main(void) {
    RUN_SUITE(all_tests);
    return 0;
}
```

### Test Macros

- `TEST(name)` - Define a test function
- `ASSERT(condition, message)` - Assert condition is true
- `ASSERT_EQ(actual, expected, message)` - Assert equality
- `ASSERT_STR_EQ(actual, expected, message)` - Assert string equality
- `RUN_TEST(name)` - Run a test
- `RUN_SUITE(suite)` - Run test suite and print results

### Running Tests

```bash
make test                     # Run all tests
./bin/test_vm_test_value     # Run specific test
```

## Development Workflow

1. **Write tests first** (TDD approach)
2. **Implement feature** to pass tests
3. **Run tests:**
   ```bash
   make test
   ```
4. **Debug if needed:**
   ```bash
   make debug
   gdb ./bin/test_vm_test_value
   ```

## Project Structure

```
include/          # Public headers
  beerlang.h      # Main header
  value.h         # Value representation
  vm.h            # VM interface
  test.h          # Test framework

src/              # Implementation
  vm/             # VM implementation
  types/          # Type implementations
  repl/           # REPL
  main.c          # Main entry point

tests/            # Test files
  vm/             # VM tests
  types/          # Type tests
  ...
```

## Next Steps

1. Check `TODO.md` for implementation priorities
2. Read `CLAUDE.md` for complete design
3. Start with Phase 1: Foundation
   - Implement value representation
   - Implement basic VM
   - Write tests for everything

## Tips

- **Keep it simple**: Start with minimal implementations
- **Test everything**: Write tests before code
- **Small commits**: Commit after each working feature
- **Read the design**: CLAUDE.md has all the details

## Getting Help

- Read the design doc: `CLAUDE.md`
- Check TODOs: `TODO.md`
- Look at existing tests for examples
