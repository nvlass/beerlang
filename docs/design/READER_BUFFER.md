# Reader Buffering Design

**Author:** Claude
**Date:** 2026-01-29
**Status:** Implemented (Phase 4.5)

## Overview

The beerlang reader uses a two-level abstraction to separate I/O (buffering) from parsing (reader logic). This design enables reading from both complete strings and streaming file input without complicating the core reader implementation.

## Architecture

### Level 1: ReadBuffer (I/O Layer)

`ReadBuffer` provides a unified interface for accessing input data, regardless of source:

```c
typedef struct ReadBuffer {
    BufferSourceType type;

    /* String source */
    const char* string;      /* Non-owning pointer */
    size_t string_len;

    /* File source */
    FILE* file;              /* Owned FILE* */
    char* data;              /* Owned buffer */
    size_t size;             /* Valid data in buffer */
    size_t capacity;         /* Total capacity */

    bool eof;
    bool error;
} ReadBuffer;
```

**Key API:**
```c
char buffer_at(ReadBuffer* buf, size_t pos);
```

This single function hides all complexity:
- For strings: Direct array access
- For files: Automatic refilling when position exceeds buffered data

### Level 2: Reader (Parse Layer)

The reader uses `ReadBuffer` through simple character access:

```c
typedef struct Reader {
    ReadBuffer* buffer;   /* Owns this */
    size_t pos;          /* Position in buffer */
    size_t line;         /* Line number */
    size_t column;       /* Column number */
    const char* filename;
    bool error;
    char error_msg[512];
} Reader;

/* All character access goes through buffer */
static char peek(Reader* r) {
    return buffer_at(r->buffer, r->pos);
}
```

## Implementation Details

### String Input (Phase 4 - Complete)

For complete strings in memory:
```c
Reader* r = reader_new("(+ 1 2)", "<string>");
Value form = reader_read(r);
reader_free(r);
```

- String pointer is non-owning (caller must keep string alive)
- No buffering needed - direct access
- `eof` is always true (complete data)

### File Input (Phase 4.5 - Complete)

For buffered file reading:
```c
FILE* f = fopen("program.beer", "r");
Reader* r = reader_new_file(f, "program.beer");
Value forms = reader_read_all(r);
reader_free(r);  /* Also closes file */
```

- Reader takes ownership of FILE*
- Buffer grows automatically as needed
- Initial buffer: 4096 bytes
- Growth strategy: 4096-byte chunks
- File is closed on `reader_free()`

**Buffer Refill Algorithm:**
```c
static bool buffer_ensure(ReadBuffer* buf, size_t pos) {
    if (pos < buf->size) return true;   /* Already have it */
    if (buf->eof) return false;         /* Past EOF */

    /* Read more data */
    size_t to_read = 4096;  /* Or calculate from 'pos' */

    /* Grow buffer if needed */
    if (buf->size + to_read > buf->capacity) {
        buf->data = realloc(buf->data, new_capacity);
        buf->capacity = new_capacity;
    }

    /* Read from file */
    size_t nread = fread(buf->data + buf->size, 1, to_read, buf->file);
    buf->size += nread;

    if (nread < to_read) {
        buf->eof = true;
    }

    return pos < buf->size;
}
```

### Design Benefits

1. **Simplicity**: Reader code unchanged - still uses `peek()` and `advance()`
2. **Efficiency**: Only reads from disk when needed
3. **Flexibility**: Easy to add new input sources
4. **Memory**: Buffer only as large as needed

## Future Extensions (Phase 6 - REPL)

### fd Input for Interactive REPL

```c
/* Future API */
Reader* reader_new_fd(int fd, const char* filename);
```

For interactive terminal input:
- Read from `stdin` (or socket, pipe, etc.)
- Handle partial reads gracefully
- Support line editing integration

### Incomplete Form Detection

The REPL needs to distinguish:
1. **Complete form** - Execute it
2. **EOF** - Exit REPL
3. **Incomplete form** - Prompt for more input

```c
/* Future API */
bool reader_is_incomplete(Reader* r);

/* REPL usage */
void repl_loop() {
    while (true) {
        printf("user=> ");
        Reader* r = reader_new_fd(STDIN_FILENO, "<repl>");
        Value form = reader_read(r);

        if (reader_has_error(r)) {
            if (reader_is_incomplete(r)) {
                /* Multi-line input */
                printf("...    ");
                /* Continue reading */
            } else {
                /* Actual error */
                fprintf(stderr, "Error: %s\n", reader_error_msg(r));
            }
        }

        /* Compile and execute form */
        // ...
    }
}
```

**Implementation Strategy:**

Incomplete detection can use EOF state:
- `reader_read()` reaches EOF mid-form → incomplete
- Track parse state: inside `(`, `[`, `{`, `"` → incomplete
- No incomplete state and got form → complete

Example:
```
user=> (+ 1
...       2)     ; Incomplete - inside '('
=> 3

user=> (+ 1 2)  ; Complete - balanced
=> 3
```

### Multi-line String Prompts

Special challenge: strings with newlines
```
user=> "hello
...    world"
```

Need to distinguish:
- Newline inside string (incomplete)
- Newline outside string (complete)

Solution: Track parse context in reader
```c
typedef enum {
    CONTEXT_TOP_LEVEL,
    CONTEXT_IN_STRING,
    CONTEXT_IN_LIST,
    CONTEXT_IN_VECTOR,
    CONTEXT_IN_MAP,
} ParseContext;
```

## Testing Strategy

### Current Tests (Phase 4.5)

```c
TEST(test_read_from_file) {
    FILE* f = tmpfile();
    fprintf(f, "(+ 1 2)\n[3 4 5]\n{:a 1 :b 2}");
    rewind(f);

    Reader* r = reader_new_file(f, "<temp>");
    Value v1 = reader_read(r);  /* (+ 1 2) */
    Value v2 = reader_read(r);  /* [3 4 5] */
    Value v3 = reader_read(r);  /* {:a 1 :b 2} */
    Value v4 = reader_read(r);  /* nil - EOF */

    reader_free(r);
}
```

### Future Tests (Phase 6)

```c
TEST(test_incomplete_form) {
    Reader* r = reader_new("(+ 1", "<test>");
    Value form = reader_read(r);
    ASSERT(reader_is_incomplete(r), "Should detect incomplete");
}

TEST(test_multiline_string) {
    Reader* r = reader_new("\"hello\nworld\"", "<test>");
    Value form = reader_read(r);
    ASSERT(!reader_has_error(r), "Newline in string is valid");
}
```

## Performance Characteristics

### String Input
- **Time**: O(1) character access
- **Space**: O(1) - no allocation, non-owning pointer

### File Input
- **Time**: Amortized O(1) character access
  - Most reads: O(1) cached
  - Occasional refills: O(chunk_size)
- **Space**: O(file_size) worst case, O(largest_form) typical
  - Buffer grows to size of largest form read
  - Small programs: ~4KB buffer
  - Large files: Buffer matches largest top-level form

### fd Input (Future)
- **Time**: Same as file input
- **Space**: O(largest_incomplete_form)
  - REPL: Typically < 1KB per expression
  - Large multiline: Could grow, but user controls input

## Alternative Designs Considered

### 1. Always Load Entire Input

**Rejected:** Memory inefficient for large files
```c
char* contents = read_entire_file(filename);
Reader* r = reader_new(contents, filename);
```

### 2. Reader Does I/O Directly

**Rejected:** Mixes concerns, complicated API
```c
/* BAD: Reader coupled to I/O */
Reader* r = reader_new_file(FILE* f);
/* Now reader has both parsing AND I/O logic */
```

### 3. Callback-based Reading

**Rejected:** Complex API, awkward error handling
```c
typedef char (*ReadCharFn)(void* ctx, size_t pos);
Reader* reader_new_callback(ReadCharFn fn, void* ctx);
```

### 4. Coroutine-based Streaming

**Rejected:** Over-engineered for current needs
- Coroutines add significant complexity
- Can revisit if needed for advanced streaming

## Conclusion

The current two-level design (ReadBuffer + Reader) provides:
- ✅ Simple reader implementation (parse logic)
- ✅ Clean separation of concerns (I/O vs parsing)
- ✅ Support for both strings and files
- ✅ Easy extension path for REPL (Phase 6)
- ✅ Good performance characteristics
- ✅ Minimal memory overhead

**Status:** Phase 4.5 complete. File reading works. REPL support deferred to Phase 6 as planned.
