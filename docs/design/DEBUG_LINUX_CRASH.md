# Linux/Xeon REPL Crash — RESOLVED

## Symptom
REPL crashed on startup on Linux/Xeon (GCC) with segfault in `strdup` called from `function_set_ns_name`. `ns->name` contained `0x10` instead of a valid pointer.

## Root Cause
**Missing POSIX feature test macro.** `-std=c11` suppresses POSIX extensions in `<string.h>`, so `strdup` (and `open_memstream`) had no declaration. GCC assumed they returned `int` (implicit function declaration), truncating the 64-bit heap pointer to 32 bits via `cltq`. ASAN maps its heap at high addresses like `0x600000000010`, so the lower 32 bits become `0x10`.

## Fix
Added `-D_DEFAULT_SOURCE` to all CFLAGS variants in the Makefile. No code changes needed.

## Lesson Learned
When using `-std=c11` on Linux, POSIX functions like `strdup`, `open_memstream`, `strndup` etc. are NOT declared unless you define `_DEFAULT_SOURCE` (or `_POSIX_C_SOURCE`/`_GNU_SOURCE`). On macOS/Clang this isn't an issue because the system headers expose these functions regardless.

**Warning signs:** GCC warnings about implicit function declarations for `strdup` — easy to miss among other warnings.
