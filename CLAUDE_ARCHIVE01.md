# Beerlang Language Design

**A pun on "Be Erlang" - but with its own identity**

## Core Philosophy

Beerlang is a LISP-family language that combines:
- Clojure's elegant syntax and data-oriented programming model
- A cache-efficient virtual machine architecture
- Cooperative multitasking for efficient concurrency
- REPL-driven interactive development

## Language Overview

### Syntax and Paradigm

- **Syntax**: Identical to Clojure (LISP family with Clojure's simplifications)
- **Core model**: Minimal special forms + macros building everything else
- **Development**: REPL-based interactive development
- **Namespaces**: Collections of symbols resolving to vars or functions (Clojure-style)

### Compilation and Execution

- **Compilation**: On-the-fly compilation to bytecode (JIT planned for later)
- **REPL Model**: Read → Compile → Execute → Print (no separate interpreter)
  - All code, including REPL expressions, is compiled to bytecode before execution
  - Function definitions are compiled when entered at the REPL
  - Simple expressions are compiled into temporary code segments
  - Ensures consistency and performance across REPL and file-based code
- **VM Architecture**: Stack-based virtual machine
- **Cache Efficiency**: VM designed to fit in L2/L3 cache (possibly with the program itself)
- **Bytecode**: Custom bytecode format executed by the VM

### Concurrency Model

- **Cooperative Multitasking**: All "basic blocks" of code can yield and resume
- **Execution**: Basic blocks are executed by actual CPU threads
- **FFI**: Can load and call shared libraries (dispatched on separate threads)

### Data Types and Structures

Following Clojure's data model as closely as possible:

- **Numbers**: Arbitrary precision arithmetic (using GMP library - Bruno Levy's port on GitHub)
- **Core Collections**:
  - Lists (sequential)
  - Vectors (sequential, indexed)
  - Hash maps (associative)
- **Type System**: Follow Clojure's type model

### Special Forms

Special forms are the minimal set of primitives that cannot be implemented as functions or macros. They control evaluation, affect lexical scope, or provide fundamental operations.

#### Core Special Forms

1. **`def`** - Define a var in the current namespace
   ```clojure
   (def x 42)
   (def my-fn (fn [a] (+ a 1)))
   ```

2. **`if`** - Conditional evaluation (only evaluates chosen branch)
   ```clojure
   (if test then-expr else-expr)
   ```

3. **`do`** - Sequential evaluation, returns last expression
   ```clojure
   (do expr1 expr2 expr3)
   ```

4. **`let`** - Lexical bindings
   ```clojure
   (let [x 10
         y 20]
     (+ x y))
   ```

5. **`quote`** - Prevent evaluation
   ```clojure
   (quote (1 2 3))  ; or '(1 2 3)
   ```

6. **`fn`** - Create anonymous function/closure
   ```clojure
   (fn [x y] (+ x y))
   (fn factorial [n] (if (< n 2) 1 (* n (factorial (- n 1)))))
   ```

7. **`loop`/`recur`** - Structured iteration (optional, can use named fn)
   ```clojure
   (loop [i 0 acc 1]
     (if (< i 10)
       (recur (+ i 1) (* acc 2))
       acc))
   ```
   Note: With implicit tail calls, `loop`/`recur` is syntactic sugar. You can achieve the same with a named function:
   ```clojure
   ((fn loop-fn [i acc]
      (if (< i 10)
        (loop-fn (+ i 1) (* acc 2))  ; Automatically tail-optimized
        acc))
    0 1)
   ```

8. **`try`/`catch`/`finally`** - Exception handling
   ```clojure
   (try
     (risky-operation)
     (catch Exception e
       (handle-error e))
     (finally
       (cleanup)))
   ```

9. **`throw`** - Raise an exception
   ```clojure
   (throw (Exception. "Error message"))
   ```

10. **`yield`** - Cooperative yielding (unique to beerlang!)
    ```clojure
    (yield)  ; Allow other tasks to run
    ```

11. **`var`** - Get the var object itself (not its value)
    ```clojure
    (var my-function)  ; or #'my-function
    ```

12. **`.`** - Interop/method call for FFI
    ```clojure
    (. object method arg1 arg2)
    ```

13. **`disasm`** - Disassemble bytecode (for metaprogramming and debugging)
    ```clojure
    (disasm my-function)
    ;; Returns bytecode as data structure (list of instruction vectors)
    ```

14. **`asm`** - Assemble bytecode from data structure
    ```clojure
    (asm [[ENTER 0]
          [LOAD_LOCAL 0]
          [INC]
          [RETURN]])
    ;; Returns a function object with the specified bytecode
    ```

These special forms enable powerful metaprogramming and optimization workflows. See the Bytecode Metaprogramming section for details.

#### Everything Else is Macros

Forms like `defn`, `when`, `cond`, `and`, `or`, `->`, `->>`, etc. are all implemented as macros on top of these special forms.

### Reader

The reader transforms text into data structures (S-expressions). It follows Clojure's reader syntax closely.

#### Reader Structure

```c
struct Reader {
    const char*  source;        // Source text
    int          pos;           // Current position
    int          line;          // Current line number
    int          column;        // Current column
    const char*  filename;      // Source filename (for errors)

    // Lookahead
    char         current_char;

    // Error state
    bool         error;
    char         error_msg[256];
};

typedef enum {
    TOK_EOF,
    TOK_LPAREN,       // (
    TOK_RPAREN,       // )
    TOK_LBRACKET,     // [
    TOK_RBRACKET,     // ]
    TOK_LBRACE,       // {
    TOK_RBRACE,       // }
    TOK_QUOTE,        // '
    TOK_BACKTICK,     // `
    TOK_TILDE,        // ~
    TOK_TILDE_AT,     // ~@
    TOK_CARET,        // ^
    TOK_AT,           // @
    TOK_HASH,         // # (dispatch)
    TOK_NUMBER,       // 123, 3.14, 1/2, 0xFF
    TOK_STRING,       // "hello"
    TOK_SYMBOL,       // foo, bar/baz
    TOK_KEYWORD,      // :foo, :bar/baz
    TOK_CHAR,         // \a, \newline
    TOK_NIL,          // nil
    TOK_TRUE,         // true
    TOK_FALSE,        // false
} TokenType;
```

#### Basic Reading

```c
// Main entry point
Value read(Reader* r) {
    skip_whitespace_and_comments(r);

    char ch = peek(r);

    switch (ch) {
        case '\0': return EOF_VALUE;
        case '(':  return read_list(r);
        case '[':  return read_vector(r);
        case '{':  return read_map(r);
        case ')':
        case ']':
        case '}':
            reader_error(r, "Unexpected closing delimiter");
            return NULL;
        case '\'': return read_quote(r);
        case '`':  return read_syntax_quote(r);
        case '~':  return read_unquote(r);
        case '^':  return read_metadata(r);
        case '@':  return read_deref(r);
        case '#':  return read_dispatch(r);
        case '"':  return read_string(r);
        case '\\': return read_char(r);
        case ':':  return read_keyword(r);
        default:
            if (is_digit(ch) || (ch == '-' && is_digit(peek_next(r)))) {
                return read_number(r);
            } else if (is_symbol_start(ch)) {
                return read_symbol(r);
            } else {
                reader_error(r, "Unexpected character: %c", ch);
                return NULL;
            }
    }
}
```

#### Reading Lists, Vectors, Maps

```c
Value read_list(Reader* r) {
    consume(r, '(');
    skip_whitespace_and_comments(r);

    Vector* elements = vector_new();

    while (peek(r) != ')' && peek(r) != '\0') {
        Value elem = read(r);
        if (r->error) {
            return NULL;
        }
        vector_push(elements, elem);
        skip_whitespace_and_comments(r);
    }

    if (peek(r) != ')') {
        reader_error(r, "Expected ')' but got EOF");
        return NULL;
    }
    consume(r, ')');

    // Convert vector to list
    return vector_to_list(elements);
}

Value read_vector(Reader* r) {
    consume(r, '[');
    skip_whitespace_and_comments(r);

    Vector* elements = vector_new();

    while (peek(r) != ']' && peek(r) != '\0') {
        Value elem = read(r);
        if (r->error) return NULL;
        vector_push(elements, elem);
        skip_whitespace_and_comments(r);
    }

    if (peek(r) != ']') {
        reader_error(r, "Expected ']' but got EOF");
        return NULL;
    }
    consume(r, ']');

    return make_vector(elements);
}

Value read_map(Reader* r) {
    consume(r, '{');
    skip_whitespace_and_comments(r);

    Vector* elements = vector_new();

    while (peek(r) != '}' && peek(r) != '\0') {
        Value key = read(r);
        if (r->error) return NULL;
        skip_whitespace_and_comments(r);

        if (peek(r) == '}' || peek(r) == '\0') {
            reader_error(r, "Map literal must contain even number of forms");
            return NULL;
        }

        Value val = read(r);
        if (r->error) return NULL;

        vector_push(elements, key);
        vector_push(elements, val);
        skip_whitespace_and_comments(r);
    }

    if (peek(r) != '}') {
        reader_error(r, "Expected '}' but got EOF");
        return NULL;
    }
    consume(r, '}');

    return vector_to_map(elements);
}
```

#### Reader Macros

```c
// ' - Quote
Value read_quote(Reader* r) {
    consume(r, '\'');
    Value form = read(r);
    if (r->error) return NULL;

    // (quote form)
    return make_list(intern_symbol("quote"), form);
}

// @ - Deref
Value read_deref(Reader* r) {
    consume(r, '@');
    Value form = read(r);
    if (r->error) return NULL;

    // (deref form)
    return make_list(intern_symbol("deref"), form);
}

// ^ - Metadata
Value read_metadata(Reader* r) {
    consume(r, '^');
    Value meta = read(r);
    if (r->error) return NULL;

    Value form = read(r);
    if (r->error) return NULL;

    // (with-meta form meta)
    return make_list(intern_symbol("with-meta"), form, meta);
}

// ` - Syntax quote (more complex, needs walking)
Value read_syntax_quote(Reader* r) {
    consume(r, '`');
    Value form = read(r);
    if (r->error) return NULL;

    return syntax_quote_expand(form);  // Recursive expansion
}

// ~ and ~@ - Unquote
Value read_unquote(Reader* r) {
    consume(r, '~');
    bool splice = false;

    if (peek(r) == '@') {
        consume(r, '@');
        splice = true;
    }

    Value form = read(r);
    if (r->error) return NULL;

    Symbol* sym = splice ? intern_symbol("unquote-splicing")
                         : intern_symbol("unquote");
    return make_list(sym, form);
}
```

#### Dispatch Reader Macros

```c
Value read_dispatch(Reader* r) {
    consume(r, '#');
    char ch = peek(r);

    switch (ch) {
        case '\'':
            // #' - Var quote
            consume(r, '\'');
            Value form = read(r);
            return make_list(intern_symbol("var"), form);

        case '{':
            // #{} - Set literal
            return read_set(r);

        case '_':
            // #_ - Discard next form
            consume(r, '_');
            read(r);  // Read and discard
            return read(r);  // Return next form

        case '(':
            // #() - Anonymous function literal
            return read_anon_fn(r);

        case '"':
            // #"" - Regular expression (treat as string for now)
            return read_regex(r);

        default:
            if (is_symbol_start(ch)) {
                // Tagged literal: #tag value
                return read_tagged_literal(r);
            }
            reader_error(r, "Unknown dispatch: #%c", ch);
            return NULL;
    }
}

// #() - Anonymous function
Value read_anon_fn(Reader* r) {
    // #(+ % %2) => (fn [%1 %2] (+ %1 %2))
    consume(r, '(');

    // Track % args while reading body
    // This is complex - needs special handling
    // For now, simplified version

    Vector* body_forms = vector_new();
    Set* args_seen = set_new();

    while (peek(r) != ')' && peek(r) != '\0') {
        Value form = read_with_arg_tracking(r, args_seen);
        vector_push(body_forms, form);
        skip_whitespace_and_comments(r);
    }

    consume(r, ')');

    // Generate arg list [%1 %2 ... %n] or [% %2 %3]
    Vector* args = generate_anon_fn_args(args_seen);
    Value body = vector_to_list(body_forms);

    // (fn [args...] body...)
    return make_list(intern_symbol("fn"), make_vector(args), body);
}
```

#### Number Reading

```c
Value read_number(Reader* r) {
    char buf[256];
    int i = 0;

    // Handle sign
    if (peek(r) == '-' || peek(r) == '+') {
        buf[i++] = advance(r);
    }

    // Read digits, dots, slashes
    bool has_dot = false;
    bool has_slash = false;
    bool is_hex = false;

    // Check for hex prefix
    if (peek(r) == '0' && (peek_next(r) == 'x' || peek_next(r) == 'X')) {
        buf[i++] = advance(r);  // '0'
        buf[i++] = advance(r);  // 'x'
        is_hex = true;
    }

    while (is_digit(peek(r)) ||
           (is_hex && is_hex_digit(peek(r))) ||
           peek(r) == '.' ||
           peek(r) == '/' ||
           peek(r) == 'e' || peek(r) == 'E') {

        char ch = peek(r);

        if (ch == '.') {
            if (has_dot || has_slash) {
                reader_error(r, "Invalid number format");
                return NULL;
            }
            has_dot = true;
        } else if (ch == '/') {
            if (has_dot || has_slash) {
                reader_error(r, "Invalid number format");
                return NULL;
            }
            has_slash = true;
        }

        buf[i++] = advance(r);
    }

    buf[i] = '\0';

    // Parse based on format
    if (has_slash) {
        // Ratio: 1/2, 22/7
        return parse_ratio(buf);
    } else if (has_dot || strchr(buf, 'e') || strchr(buf, 'E')) {
        // Float: 3.14, 1e10
        return make_float(strtod(buf, NULL));
    } else if (is_hex) {
        // Hex integer: 0xFF
        return make_integer(strtoll(buf, NULL, 16));
    } else {
        // Integer
        long long val = strtoll(buf, NULL, 10);
        if (errno == ERANGE || val < FIXNUM_MIN || val > FIXNUM_MAX) {
            // Use bigint
            return make_bigint_from_string(buf, 10);
        }
        return make_fixnum(val);
    }
}
```

#### String Reading

```c
Value read_string(Reader* r) {
    consume(r, '"');

    char buf[4096];
    int i = 0;

    while (peek(r) != '"' && peek(r) != '\0') {
        if (peek(r) == '\\') {
            // Escape sequence
            advance(r);
            char escaped = advance(r);

            switch (escaped) {
                case 'n':  buf[i++] = '\n'; break;
                case 't':  buf[i++] = '\t'; break;
                case 'r':  buf[i++] = '\r'; break;
                case '\\': buf[i++] = '\\'; break;
                case '"':  buf[i++] = '"'; break;
                case 'u':
                    // Unicode escape: \uXXXX
                    int codepoint = read_unicode_escape(r, 4);
                    i += encode_utf8(buf + i, codepoint);
                    break;
                default:
                    reader_error(r, "Unknown escape sequence: \\%c", escaped);
                    return NULL;
            }
        } else {
            buf[i++] = advance(r);
        }
    }

    if (peek(r) != '"') {
        reader_error(r, "Unterminated string");
        return NULL;
    }
    consume(r, '"');

    buf[i] = '\0';
    return make_string(buf, i);
}
```

#### Symbol and Keyword Reading

```c
Value read_symbol(Reader* r) {
    char buf[256];
    int i = 0;

    // Read symbol characters
    while (is_symbol_char(peek(r))) {
        buf[i++] = advance(r);
    }
    buf[i] = '\0';

    // Check for special values
    if (strcmp(buf, "nil") == 0) return make_nil();
    if (strcmp(buf, "true") == 0) return make_true();
    if (strcmp(buf, "false") == 0) return make_false();

    // Parse namespace/name
    char* slash = strchr(buf, '/');
    if (slash != NULL) {
        // Qualified symbol: namespace/name
        *slash = '\0';
        return intern_qualified_symbol(buf, slash + 1);
    } else {
        // Unqualified symbol
        return intern_symbol(buf);
    }
}

Value read_keyword(Reader* r) {
    consume(r, ':');

    // Keywords can also start with ::
    bool auto_resolved = false;
    if (peek(r) == ':') {
        consume(r, ':');
        auto_resolved = true;
    }

    char buf[256];
    int i = 0;

    while (is_symbol_char(peek(r))) {
        buf[i++] = advance(r);
    }
    buf[i] = '\0';

    if (auto_resolved) {
        // ::name => :current-namespace/name
        return intern_qualified_keyword(current_namespace_name(), buf);
    }

    // Parse namespace/name
    char* slash = strchr(buf, '/');
    if (slash != NULL) {
        *slash = '\0';
        return intern_qualified_keyword(buf, slash + 1);
    } else {
        return intern_keyword(buf);
    }
}
```

#### Character Reading

```c
Value read_char(Reader* r) {
    consume(r, '\\');

    char buf[32];
    int i = 0;

    // Read character name or single char
    while (is_alpha(peek(r))) {
        buf[i++] = advance(r);
    }

    if (i == 0) {
        // Single character
        return make_char(advance(r));
    }

    buf[i] = '\0';

    // Named characters
    if (strcmp(buf, "newline") == 0) return make_char('\n');
    if (strcmp(buf, "space") == 0) return make_char(' ');
    if (strcmp(buf, "tab") == 0) return make_char('\t');
    if (strcmp(buf, "return") == 0) return make_char('\r');
    if (strcmp(buf, "backspace") == 0) return make_char('\b');
    if (strcmp(buf, "formfeed") == 0) return make_char('\f');

    // Unicode: \uXXXX
    if (buf[0] == 'u' && i == 5) {
        int codepoint = parse_hex(&buf[1], 4);
        return make_char(codepoint);
    }

    // Single named character
    if (i == 1) {
        return make_char(buf[0]);
    }

    reader_error(r, "Unknown character: \\%s", buf);
    return NULL;
}
```

#### Comments and Whitespace

```c
void skip_whitespace_and_comments(Reader* r) {
    while (true) {
        char ch = peek(r);

        if (is_whitespace(ch)) {
            if (ch == '\n') {
                r->line++;
                r->column = 0;
            }
            advance(r);
        } else if (ch == ';') {
            // Line comment - skip until newline
            while (peek(r) != '\n' && peek(r) != '\0') {
                advance(r);
            }
        } else {
            break;
        }
    }
}

bool is_whitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' ||
           ch == '\r' || ch == ',';  // Comma is whitespace in Clojure!
}
```

#### Error Handling

```c
void reader_error(Reader* r, const char* fmt, ...) {
    r->error = true;

    va_list args;
    va_start(args, fmt);

    snprintf(r->error_msg, sizeof(r->error_msg),
             "%s:%d:%d: ", r->filename, r->line, r->column);

    int offset = strlen(r->error_msg);
    vsnprintf(r->error_msg + offset, sizeof(r->error_msg) - offset,
              fmt, args);

    va_end(args);
}
```

#### Reader Features

**Supported syntax:**
- Lists: `(1 2 3)`
- Vectors: `[1 2 3]`
- Maps: `{:a 1 :b 2}`
- Sets: `#{1 2 3}`
- Strings: `"hello\nworld"`
- Numbers: `42`, `3.14`, `1/2`, `0xFF`, `1e10`
- Characters: `\a`, `\newline`, `\u03BB`
- Symbols: `foo`, `bar/baz`
- Keywords: `:foo`, `:bar/baz`, `::local`
- Special values: `nil`, `true`, `false`
- Comments: `; line comment`
- Quote: `'form` → `(quote form)`
- Deref: `@form` → `(deref form)`
- Metadata: `^{:doc "..."} form`
- Var quote: `#'foo` → `(var foo)`
- Anonymous fn: `#(+ % %2)`
- Discard: `#_ignored`
- Regex: `#"pattern"` (optional)

**Not supported initially:**
- Reader conditionals (`#?`, `#?@`) - for future
- Tagged literals beyond built-ins - for future
- Namespaced maps (`#:ns{:a 1}`) - for future

### Printer

The printer transforms data structures into text representation. It provides both human-readable and machine-readable output, with support for all beerlang data types.

#### Printer Structure

```c
struct Printer {
    StringBuilder* sb;       // Output buffer
    bool           readable; // Print for read-back vs display
    int            depth;    // Current depth (for cycle detection)
    Set*           seen;     // Objects already printed (cycle detection)

    // Pretty printing
    bool           pretty;
    int            indent_level;
    int            indent_size;
};

// StringBuilder for efficient string building
struct StringBuilder {
    char*  buffer;
    int    length;
    int    capacity;
};
```

#### Main Print Function

```c
void print(Printer* p, Value val) {
    // Check for immediate values (no allocation)
    if (is_fixnum(val)) {
        print_fixnum(p, untag_fixnum(val));
        return;
    }

    if (is_char(val)) {
        print_char(p, untag_char(val), p->readable);
        return;
    }

    if (is_special_constant(val)) {
        if (is_nil(val)) sb_append(p->sb, "nil");
        else if (is_true(val)) sb_append(p->sb, "true");
        else if (is_false(val)) sb_append(p->sb, "false");
        return;
    }

    // Heap-allocated objects
    if (!is_pointer(val)) {
        sb_append(p->sb, "#<unknown>");
        return;
    }

    Object* obj = untag_pointer(val);

    // Cycle detection (for mutable refs or cycles)
    if (set_contains(p->seen, obj)) {
        sb_append(p->sb, "#<circular>");
        return;
    }

    // Add to seen set for cycle detection
    set_add(p->seen, obj);
    p->depth++;

    // Dispatch based on type
    switch (obj->type) {
        case TYPE_BIGINT:
            print_bigint(p, (Bigint*)obj);
            break;
        case TYPE_FLOAT:
            print_float(p, (Float*)obj);
            break;
        case TYPE_STRING:
            print_string(p, (String*)obj, p->readable);
            break;
        case TYPE_SYMBOL:
            print_symbol(p, (Symbol*)obj);
            break;
        case TYPE_KEYWORD:
            print_keyword(p, (Keyword*)obj);
            break;
        case TYPE_CONS:
            print_list(p, (Cons*)obj);
            break;
        case TYPE_VECTOR:
            print_vector(p, (Vector*)obj);
            break;
        case TYPE_HASHMAP:
            print_map(p, (HashMap*)obj);
            break;
        case TYPE_FUNCTION:
            print_function(p, (Function*)obj);
            break;
        case TYPE_VAR:
            print_var(p, (Var*)obj);
            break;
        case TYPE_NAMESPACE:
            print_namespace(p, (Namespace*)obj);
            break;
        default:
            sb_append_fmt(p->sb, "#<unknown-type-%d>", obj->type);
            break;
    }

    p->depth--;
    set_remove(p->seen, obj);
}
```

#### Printing Numbers

```c
void print_fixnum(Printer* p, long long n) {
    sb_append_fmt(p->sb, "%lld", n);
}

void print_bigint(Printer* p, Bigint* bi) {
    char* str = mpz_get_str(NULL, 10, bi->value);
    sb_append(p->sb, str);
    free(str);
}

void print_float(Printer* p, Float* f) {
    // Check for special values
    if (isnan(f->value)) {
        sb_append(p->sb, "##NaN");
    } else if (isinf(f->value)) {
        if (f->value > 0) {
            sb_append(p->sb, "##Inf");
        } else {
            sb_append(p->sb, "##-Inf");
        }
    } else {
        // Print with appropriate precision
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", f->value);
        sb_append(p->sb, buf);

        // Ensure decimal point for readability
        if (strchr(buf, '.') == NULL && strchr(buf, 'e') == NULL) {
            sb_append(p->sb, ".0");
        }
    }
}

void print_ratio(Printer* p, Ratio* r) {
    sb_append_fmt(p->sb, "%lld/%lld", r->numerator, r->denominator);
}
```

#### Printing Strings and Characters

```c
void print_string(Printer* p, String* s, bool readable) {
    if (readable) {
        // Print with quotes and escapes for read-back
        sb_append(p->sb, "\"");

        for (int i = 0; i < s->header.size; i++) {
            char ch = s->data[i];
            switch (ch) {
                case '\n': sb_append(p->sb, "\\n"); break;
                case '\t': sb_append(p->sb, "\\t"); break;
                case '\r': sb_append(p->sb, "\\r"); break;
                case '\\': sb_append(p->sb, "\\\\"); break;
                case '"':  sb_append(p->sb, "\\\""); break;
                default:
                    if (ch >= 32 && ch < 127) {
                        sb_append_char(p->sb, ch);
                    } else {
                        // Non-printable - use unicode escape
                        sb_append_fmt(p->sb, "\\u%04X", (unsigned char)ch);
                    }
                    break;
            }
        }

        sb_append(p->sb, "\"");
    } else {
        // Print without quotes for display
        sb_append_len(p->sb, s->data, s->header.size);
    }
}

void print_char(Printer* p, uint32_t codepoint, bool readable) {
    if (readable) {
        sb_append(p->sb, "\\");

        // Named characters
        switch (codepoint) {
            case '\n': sb_append(p->sb, "newline"); return;
            case ' ':  sb_append(p->sb, "space"); return;
            case '\t': sb_append(p->sb, "tab"); return;
            case '\r': sb_append(p->sb, "return"); return;
            case '\b': sb_append(p->sb, "backspace"); return;
            case '\f': sb_append(p->sb, "formfeed"); return;
        }

        // Regular character
        if (codepoint >= 32 && codepoint < 127) {
            sb_append_char(p->sb, (char)codepoint);
        } else {
            // Unicode escape
            sb_append_fmt(p->sb, "u%04X", codepoint);
        }
    } else {
        // Display mode - just the character
        char buf[8];
        int len = encode_utf8(buf, codepoint);
        sb_append_len(p->sb, buf, len);
    }
}
```

#### Printing Symbols and Keywords

```c
void print_symbol(Printer* p, Symbol* sym) {
    if (sym->ns != NULL) {
        // Qualified symbol: namespace/name
        sb_append(p->sb, sym->ns->name->data);
        sb_append(p->sb, "/");
    }
    sb_append(p->sb, sym->name);
}

void print_keyword(Printer* p, Keyword* kw) {
    sb_append(p->sb, ":");

    if (kw->ns != NULL) {
        // Qualified keyword: :namespace/name
        sb_append(p->sb, kw->ns->name->data);
        sb_append(p->sb, "/");
    }
    sb_append(p->sb, kw->name);
}
```

#### Printing Collections

```c
void print_list(Printer* p, Cons* list) {
    sb_append(p->sb, "(");

    bool first = true;
    Cons* current = list;

    while (current != NULL) {
        if (!first) {
            sb_append(p->sb, " ");
            if (p->pretty) maybe_newline(p);
        }
        first = false;

        print(p, current->first);

        Value rest = current->rest;
        if (is_nil(rest)) {
            break;
        } else if (is_cons(rest)) {
            current = (Cons*)untag_pointer(rest);
        } else {
            // Improper list (shouldn't happen in beerlang)
            sb_append(p->sb, " . ");
            print(p, rest);
            break;
        }
    }

    sb_append(p->sb, ")");
}

void print_vector(Printer* p, Vector* vec) {
    sb_append(p->sb, "[");

    for (int i = 0; i < vec->header.size; i++) {
        if (i > 0) {
            sb_append(p->sb, " ");
            if (p->pretty) maybe_newline(p);
        }
        print(p, vector_get(vec, i));
    }

    sb_append(p->sb, "]");
}

void print_map(Printer* p, HashMap* map) {
    sb_append(p->sb, "{");

    if (p->pretty) {
        p->indent_level++;
    }

    // Iterate over map entries
    MapIterator* it = map_iterator_new(map);
    bool first = true;

    while (map_iterator_has_next(it)) {
        MapEntry entry = map_iterator_next(it);

        if (!first) {
            if (p->pretty) {
                sb_append(p->sb, "\n");
                print_indent(p);
            } else {
                sb_append(p->sb, " ");
            }
        }
        first = false;

        print(p, entry.key);
        sb_append(p->sb, " ");
        print(p, entry.value);
    }

    if (p->pretty) {
        p->indent_level--;
    }

    sb_append(p->sb, "}");

    map_iterator_free(it);
}

void print_set(Printer* p, HashSet* set) {
    sb_append(p->sb, "#{");

    SetIterator* it = set_iterator_new(set);
    bool first = true;

    while (set_iterator_has_next(it)) {
        if (!first) sb_append(p->sb, " ");
        first = false;

        print(p, set_iterator_next(it));
    }

    sb_append(p->sb, "}");

    set_iterator_free(it);
}
```

#### Printing Functions and Special Objects

```c
void print_function(Printer* p, Function* fn) {
    // Print non-readable representation
    sb_append(p->sb, "#<function");

    // Try to get function name from metadata or var
    // (simplified - would need to search namespace vars)

    if (fn->header.size >= 0) {
        // Fixed arity
        sb_append_fmt(p->sb, " arity=%d", fn->header.size);
    } else {
        sb_append(p->sb, " variadic");
    }

    sb_append_fmt(p->sb, " @%p>", (void*)fn);
}

void print_native_function(Printer* p, NativeFunction* nfn) {
    sb_append(p->sb, "#<native-function");

    if (nfn->header.size >= 0) {
        sb_append_fmt(p->sb, " arity=%d", nfn->header.size);
    }

    sb_append_fmt(p->sb, " @%p>", nfn->fn_ptr);
}

void print_var(Printer* p, Var* var) {
    sb_append(p->sb, "#'");
    print_symbol(p, var->name);
}

void print_namespace(Printer* p, Namespace* ns) {
    sb_append(p->sb, "#<Namespace ");
    print_string(p, ns->name, false);
    sb_append(p->sb, ">");
}

void print_channel(Printer* p, Channel* ch) {
    sb_append_fmt(p->sb, "#<Channel capacity=%d size=%d @%p>",
                  ch->capacity, queue_size(ch->buffer), (void*)ch);
}

void print_task(Printer* p, Task* task) {
    const char* state_str = task_state_string(task->state);
    sb_append_fmt(p->sb, "#<Task id=%llu state=%s>",
                  task->id, state_str);
}
```

#### Pretty Printing

```c
void maybe_newline(Printer* p) {
    if (p->pretty && p->sb->length > 80) {
        sb_append(p->sb, "\n");
        print_indent(p);
    }
}

void print_indent(Printer* p) {
    for (int i = 0; i < p->indent_level * p->indent_size; i++) {
        sb_append_char(p->sb, ' ');
    }
}
```

#### Public API

```c
// Print for REPL (readable)
char* pr_str(Value val) {
    Printer* p = printer_new(true, false);
    print(p, val);
    char* result = sb_to_string(p->sb);
    printer_free(p);
    return result;
}

// Print for display (not readable)
char* print_str(Value val) {
    Printer* p = printer_new(false, false);
    print(p, val);
    char* result = sb_to_string(p->sb);
    printer_free(p);
    return result;
}

// Pretty print
char* pprint(Value val) {
    Printer* p = printer_new(true, true);
    p->indent_size = 2;
    print(p, val);
    char* result = sb_to_string(p->sb);
    printer_free(p);
    return result;
}

// Print to stdout (for println)
void println(Value val) {
    char* s = print_str(val);
    printf("%s\n", s);
    free(s);
}

// Print to stdout (for prn - readable)
void prn(Value val) {
    char* s = pr_str(val);
    printf("%s\n", s);
    free(s);
}
```

#### Printer Modes

**Readable mode** (`pr-str`, `prn`):
- Strings printed with quotes and escapes
- Characters printed as `\a`, `\newline`, etc.
- Can be read back with reader
- Used for serialization

**Display mode** (`print-str`, `println`):
- Strings printed without quotes
- Characters printed as-is
- More human-friendly
- Used for output

**Pretty mode** (`pprint`):
- Adds indentation and line breaks
- Makes nested structures easier to read
- Still readable (can be read back)

#### Examples

```clojure
;; Readable printing
(pr-str "hello\nworld")      ; => "\"hello\\nworld\""
(pr-str 'foo/bar)            ; => "foo/bar"
(pr-str [1 2 3])             ; => "[1 2 3]"
(pr-str {:a 1 :b 2})         ; => "{:a 1 :b 2}"
(pr-str #'user/foo)          ; => "#'user/foo"
(pr-str \newline)            ; => "\\newline"

;; Display printing
(println "hello\nworld")     ; prints: hello
                             ;         world
(println [1 2 3])            ; prints: [1 2 3]

;; Pretty printing
(pprint {:users [{:name "Alice" :age 30}
                 {:name "Bob" :age 25}]})
; prints:
; {:users
;   [{:name "Alice"
;     :age 30}
;    {:name "Bob"
;     :age 25}]}
```

#### Cycle Detection

The printer tracks seen objects to detect cycles (rare with persistent data structures, but possible with:
- Atoms/refs containing circular references
- Metadata containing parent references
- Explicitly constructed cycles

```c
// Cycle detection example
(let [x (atom nil)]
  (reset! x x)
  (println x))  ; prints: #<Atom #<circular>>
```

#### StringBuilder Implementation

```c
StringBuilder* sb_new() {
    StringBuilder* sb = malloc(sizeof(StringBuilder));
    sb->capacity = 256;
    sb->length = 0;
    sb->buffer = malloc(sb->capacity);
    sb->buffer[0] = '\0';
    return sb;
}

void sb_append(StringBuilder* sb, const char* str) {
    sb_append_len(sb, str, strlen(str));
}

void sb_append_len(StringBuilder* sb, const char* str, int len) {
    while (sb->length + len + 1 > sb->capacity) {
        sb->capacity *= 2;
        sb->buffer = realloc(sb->buffer, sb->capacity);
    }
    memcpy(sb->buffer + sb->length, str, len);
    sb->length += len;
    sb->buffer[sb->length] = '\0';
}

void sb_append_char(StringBuilder* sb, char ch) {
    if (sb->length + 2 > sb->capacity) {
        sb->capacity *= 2;
        sb->buffer = realloc(sb->buffer, sb->capacity);
    }
    sb->buffer[sb->length++] = ch;
    sb->buffer[sb->length] = '\0';
}

void sb_append_fmt(StringBuilder* sb, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sb_append(sb, buf);
}

char* sb_to_string(StringBuilder* sb) {
    char* result = malloc(sb->length + 1);
    memcpy(result, sb->buffer, sb->length + 1);
    return result;
}
```

### Bytecode Instruction Set

The stack-based VM uses a compact instruction set designed to fit in cache. Instructions are single-byte opcodes with optional operands.

#### Stack Operations (0x00 - 0x0F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x00 | `NOP` | No operation |
| 0x01 | `POP` | Pop top of stack |
| 0x02 | `DUP` | Duplicate top of stack |
| 0x03 | `SWAP` | Swap top two stack elements |
| 0x04 | `OVER` | Copy second element to top |

#### Constants & Literals (0x10 - 0x1F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x10 | `PUSH_NIL` | Push nil |
| 0x11 | `PUSH_TRUE` | Push true |
| 0x12 | `PUSH_FALSE` | Push false |
| 0x13 | `PUSH_CONST <idx>` | Push constant from constant pool |
| 0x14 | `PUSH_INT <i8>` | Push small integer (-128 to 127) |

#### Variables & Scope (0x20 - 0x2F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x20 | `LOAD_VAR <idx>` | Load value from namespace var |
| 0x21 | `STORE_VAR <idx>` | Store to namespace var |
| 0x22 | `LOAD_LOCAL <idx>` | Load from local binding |
| 0x23 | `STORE_LOCAL <idx>` | Store to local binding |
| 0x24 | `LOAD_CLOSURE <idx>` | Load from closure environment |
| 0x25 | `GET_VAR <idx>` | Get var object (not its value) |

#### Arithmetic Operations (0x30 - 0x3F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x30 | `ADD` | Pop two, push sum |
| 0x31 | `SUB` | Pop two, push difference |
| 0x32 | `MUL` | Pop two, push product |
| 0x33 | `DIV` | Pop two, push quotient |
| 0x34 | `MOD` | Pop two, push remainder |
| 0x35 | `NEG` | Negate top of stack |
| 0x36 | `INC` | Increment top by 1 |
| 0x37 | `DEC` | Decrement top by 1 |

#### Comparison Operations (0x40 - 0x4F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x40 | `EQ` | Equal |
| 0x41 | `NEQ` | Not equal |
| 0x42 | `LT` | Less than |
| 0x43 | `LTE` | Less than or equal |
| 0x44 | `GT` | Greater than |
| 0x45 | `GTE` | Greater than or equal |

#### Logical Operations (0x50 - 0x5F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x50 | `NOT` | Logical not |
| 0x51 | `BIT_AND` | Bitwise and |
| 0x52 | `BIT_OR` | Bitwise or |
| 0x53 | `BIT_XOR` | Bitwise xor |
| 0x54 | `BIT_NOT` | Bitwise not |
| 0x55 | `SHL` | Shift left |
| 0x56 | `SHR` | Shift right |

#### Control Flow (0x60 - 0x6F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x60 | `JUMP <offset>` | Unconditional jump |
| 0x61 | `JUMP_IF_FALSE <offset>` | Jump if top is false/nil |
| 0x62 | `JUMP_IF_TRUE <offset>` | Jump if top is true |
| 0x63 | `CALL <n_args>` | Call function with n args |
| 0x64 | `TAIL_CALL <n_args>` | Tail call (compiler-generated, implicit) |
| 0x65 | `RETURN` | Return from function |
| 0x66 | `YIELD` | Cooperative yield |
| 0x67 | `ENTER <n_locals>` | Function entry, allocate locals |

#### Data Structures (0x70 - 0x7F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x70 | `MAKE_LIST <n>` | Create list from top n items |
| 0x71 | `MAKE_VECTOR <n>` | Create vector from top n items |
| 0x72 | `MAKE_MAP <n>` | Create map from top 2n items |
| 0x73 | `GET` | Get element (collection key -> value) |
| 0x74 | `ASSOC` | Associate key with value |
| 0x75 | `CONJ` | Add to collection |
| 0x76 | `FIRST` | Get first element |
| 0x77 | `REST` | Get rest of collection |
| 0x78 | `COUNT` | Get collection size |
| 0x79 | `NTH` | Get nth element (index based) |

#### Functions & Closures (0x80 - 0x8F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x80 | `MAKE_CLOSURE <code_idx> <n>` | Create closure capturing n values |
| 0x81 | `APPLY <n>` | Apply function to args |
| 0x82 | `PARTIAL <n>` | Partial function application |

#### Exception Handling (0x90 - 0x9F)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0x90 | `SETUP_TRY <catch> <finally>` | Setup try block with offsets |
| 0x91 | `END_TRY` | End try block |
| 0x92 | `THROW` | Throw exception |

#### FFI & Native Calls (0xA0 - 0xAF)

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| 0xA0 | `LOAD_LIB <idx>` | Load shared library |
| 0xA1 | `CALL_NATIVE <idx> <n>` | Call native function with n args |

#### Reserved for Future Use (0xB0 - 0xBF)

**Type Checking & Metadata** - Reserved but not initially implemented

These opcodes are reserved for potential future optimization of commonly-used operations. Initially, functionality will be provided by standard library functions:

- `0xB0-0xB2`: Type checking and metadata operations (`type-of`, `meta`, `with-meta`)
- VM will trap on these opcodes (error or dispatch to library function)
- Can be implemented natively if proven to be performance bottlenecks
- Bytecode versioning allows controlled evolution

**Rationale**: Keep VM minimal and cache-efficient. Add instructions only when measurements prove they're necessary. Old bytecode remains compatible forever.

### Bytecode Metaprogramming

Beerlang embraces "code as data" at the bytecode level through the `disasm` and `asm` special forms. This enables powerful metaprogramming, debugging, optimization, and experimentation workflows.

#### Philosophy

In Lisp tradition, code is data. Beerlang extends this to the bytecode level:
- Functions can be disassembled into data structures (lists of instructions)
- Bytecode can be inspected, analyzed, and modified
- Modified bytecode can be reassembled into executable functions
- Enables runtime code generation and optimization

#### disasm - Disassemble Bytecode

The `disasm` special form takes a function and returns its bytecode as a data structure:

```clojure
;; Define a function
(defn add-one [x]
  (+ x 1))

;; Disassemble it
(disasm add-one)

;; Returns:
;; [[ENTER 0]
;;  [LOAD_VAR <idx-of-+>]
;;  [LOAD_LOCAL 0]
;;  [PUSH_INT 1]
;;  [CALL 2]
;;  [RETURN]]
```

**Disassembly Format:**

Each instruction is represented as a vector:
- First element: instruction name (keyword or symbol)
- Remaining elements: operands

```clojure
;; Simple instructions (no operands)
[:RETURN]
[:ADD]
[:YIELD]

;; Instructions with operands
[:PUSH_INT 42]
[:LOAD_VAR 5]
[:JUMP_IF_FALSE 10]
[:CALL 3]
[:ENTER 2]

;; Instructions with labels (resolved to offsets)
[:JUMP :loop-start]
[:LABEL :loop-start]
```

**What can be disassembled:**
- Regular functions: `(disasm my-fn)`
- Anonymous functions: `(disasm (fn [x] (* x x)))`
- Closures: Shows captured values
- Vars: `(disasm #'user/foo)` - disassembles var's value

**Metadata in disassembly:**

```clojure
(disasm add-one)

;; Returns with metadata:
;; ^{:arity 1
;;   :n-locals 0
;;   :code-size 6
;;   :var #'user/add-one}
;; [[ENTER 0]
;;  [LOAD_VAR 0]
;;  [LOAD_LOCAL 0]
;;  [PUSH_INT 1]
;;  [CALL 2]
;;  [RETURN]]
```

#### asm - Assemble Bytecode

The `asm` special form takes a bytecode data structure and returns an executable function:

```clojure
;; Create a function from bytecode
(def my-fn
  (asm [[ENTER 0]
        [LOAD_LOCAL 0]
        [INC]
        [RETURN]]))

(my-fn 5)  ; => 6
```

**Assembly Features:**

1. **Labels and jumps:**
   ```clojure
   (asm [[ENTER 1]
         [:LABEL :loop]
         [LOAD_LOCAL 0]
         [PUSH_INT 0]
         [GT]
         [JUMP_IF_FALSE :end]
         [LOAD_LOCAL 0]
         [DEC]
         [STORE_LOCAL 0]
         [JUMP :loop]
         [:LABEL :end]
         [LOAD_LOCAL 1]
         [RETURN]])
   ```

2. **Arity specification:**
   ```clojure
   ;; Fixed arity
   (asm {:arity 2} bytecode)

   ;; Variadic
   (asm {:arity -1} bytecode)
   ```

3. **Closure creation:**
   ```clojure
   (let [x 10]
     ;; Create closure that captures x
     (asm {:captures [x]}
          [[ENTER 0]
           [LOAD_CLOSURE 0]  ; Load captured x
           [LOAD_LOCAL 0]     ; Load argument
           [ADD]
           [RETURN]]))
   ```

4. **Validation:**
   - `asm` validates bytecode structure
   - Checks instruction operands
   - Verifies label references
   - Checks stack depth consistency
   - Reports errors with position information

#### Use Cases

**1. Debugging and Learning:**

```clojure
;; See what the compiler generates
(defn factorial [n]
  (if (< n 2)
    1
    (* n (factorial (- n 1)))))

(println (disasm factorial))

;; Understand tail call optimization
(defn factorial-tr [n acc]
  (if (< n 2)
    acc
    (factorial-tr (- n 1) (* n acc))))

(println (disasm factorial-tr))
;; Look for TAIL_CALL instruction
```

**2. Manual Optimization:**

```clojure
;; Original function
(defn add-one [x]
  (+ x 1))

;; Disassemble and optimize
(def optimized-bytecode
  (-> (disasm add-one)
      ;; Replace [PUSH_INT 1] [ADD] with [INC]
      (replace-sequence [[PUSH_INT 1] [ADD]] [[INC]])))

(def add-one-optimized (asm optimized-bytecode))
```

**3. Runtime Code Generation:**

```clojure
;; Generate specialized functions at runtime
(defn make-adder [n]
  (asm [[ENTER 0]
        [LOAD_LOCAL 0]
        [PUSH_INT ~n]  ; Bake in the constant
        [ADD]
        [RETURN]]))

(def add-10 (make-adder 10))
(add-10 5)  ; => 15
```

**4. Peephole Optimization:**

```clojure
(defn optimize-bytecode [bytecode]
  (-> bytecode
      ;; [PUSH_INT n] [PUSH_INT 1] [ADD] -> [PUSH_INT n+1]
      (fold-constants)
      ;; [LOAD_LOCAL i] [POP] -> []
      (remove-dead-loads)
      ;; [JUMP :L] [:LABEL :L] -> [:LABEL :L]
      (remove-useless-jumps)))
```

**5. Bytecode Analysis:**

```clojure
;; Analyze instruction frequency
(defn instruction-histogram [fn]
  (->> (disasm fn)
       (map first)
       (frequencies)
       (sort-by second >)))

;; Find all function calls
(defn find-calls [fn]
  (->> (disasm fn)
       (filter #(= (first %) :CALL))
       (map second)))

;; Estimate stack depth
(defn max-stack-depth [bytecode]
  (reduce (fn [[depth max-depth] instr]
            (let [new-depth (+ depth (stack-effect instr))]
              [new-depth (max max-depth new-depth)]))
          [0 0]
          bytecode))
```

**6. JIT Preparation:**

```clojure
;; Mark hot functions for JIT compilation
(defn mark-hot-path [fn]
  (let [bc (disasm fn)]
    (asm (with-meta bc {:jit-compile true}))))
```

#### Safety and Validation

**Type Safety:**
- `asm` validates instruction operands
- Checks stack depth consistency
- Verifies jump targets exist
- Ensures local variable indices are valid

**Invalid bytecode:**

```clojure
;; Error: stack underflow
(asm [[ADD]         ; No values on stack!
      [RETURN]])

;; Error: undefined label
(asm [[JUMP :nowhere]
      [RETURN]])

;; Error: invalid operand
(asm [[PUSH_INT "not a number"]
      [RETURN]])
```

**Sandboxing:**
- Assembled bytecode runs in same sandbox as normal code
- No additional privileges
- Subject to same resource limits
- Can't escape VM

#### REPL Integration

```clojure
user=> (defn square [x] (* x x))
#'user/square

user=> (disasm square)
[[ENTER 0]
 [LOAD_VAR 2]
 [LOAD_LOCAL 0]
 [LOAD_LOCAL 0]
 [CALL 2]
 [RETURN]]

user=> (def fast-square
         (asm [[ENTER 0]
               [LOAD_LOCAL 0]
               [LOAD_LOCAL 0]
               [MUL]
               [RETURN]]))
#'user/fast-square

user=> (fast-square 5)
25
```

#### Implementation Notes

**Disassembly:**
- Function object contains bytecode pointer
- Disassembler walks bytecode, decodes instructions
- Resolves var indices to names
- Converts offsets to labels for readability

**Assembly:**
- Parser validates instruction format
- Label resolution pass
- Stack depth analysis
- Creates Function object with bytecode
- Returns executable function value

**Performance:**
- `disasm` is not optimized (debugging tool)
- `asm` validates, so has overhead
- Assembled functions run at normal VM speed
- No runtime overhead for assembled bytecode

#### Future Extensions

1. **Bytecode optimization passes:**
   ```clojure
   (asm-optimize bytecode)  ; Apply standard optimizations
   ```

2. **Bytecode transformations:**
   ```clojure
   (instrument bytecode)    ; Add profiling instrumentation
   (inline-calls bytecode)  ; Inline small function calls
   ```

3. **Pattern matching on bytecode:**
   ```clojure
   (match-bytecode bytecode
     [:PUSH_INT ?n :PUSH_INT 1 :ADD]
     => [:PUSH_INT (inc ?n)])
   ```

4. **Bytecode libraries:**
   - Standard optimization passes
   - Analysis tools
   - Code generation helpers

### Memory Management

Beerlang uses **reference counting** as its primary garbage collection strategy. This approach is particularly well-suited to the language's design:

#### Why Reference Counting Works Well Here

1. **Persistent Data Structures**: Following Clojure's model, all core data structures are immutable and persistent
   - Structural sharing creates predominantly tree-shaped object graphs
   - Cycles are rare by design (unlike mutable object graphs)
   - Most objects have clear ownership hierarchies

2. **Cache Efficiency**: Reference counting aligns with the cache-friendly VM design
   - Objects are freed immediately when unreferenced
   - Memory is reclaimed deterministically
   - No unpredictable GC pauses that could evict VM from cache

3. **Cooperative Multitasking**: Perfect fit with the concurrency model
   - No stop-the-world pauses interrupting cooperative tasks
   - Within a single task, no atomic operations needed for refcounts
   - Predictable performance characteristics

4. **Simple Implementation**: Keeps the VM small and understandable
   - Minimal GC code footprint
   - Easy to reason about and debug
   - No complex tracing algorithms

#### Handling the Challenges

**Cycles**: While rare, cycles can still occur in:
- Closures that capture themselves
- Metadata referencing parent structures
- Explicit cycles in user code

**Solutions**:
- **Weak references** for known cycle-prone patterns (closures, metadata)
- **Optional cycle detector**: Periodic scan for unreachable cycles (can be cooperative)
- **Language design**: Make cycles naturally rare (follow Clojure's lead)

**Cascade Deletes**: Large structure deallocations could cause latency spikes

**Solutions**:
- **Deferred deletion queue**: Amortize large deallocations over time
- **Cooperative yielding**: Yield during cascade deletes of deep structures
- **Threshold-based**: Only defer deletion for structures above size threshold

**Reference Counting Overhead**: Every assignment needs inc/dec operations

**Mitigations**:
- No atomic operations needed within single cooperative task
- VM-level optimizations (elide obvious inc/dec pairs)
- Stack references don't need counting (owned by frame)

#### Implementation Strategy

1. **Per-object refcount**: Small integer field in object header
2. **Stack references**: Owned by stack frames, no counting needed
3. **Heap references**: Counted normally
4. **Weak references**: Special pointer type that doesn't increment refcount
5. **Deferred deletion**: Queue for cooperative cleanup of large structures

### Object Representation

Beerlang's object representation is designed for cache efficiency, minimal memory overhead, and efficient reference counting. The design uses **tagged pointers** for immediate values and **headers** for heap-allocated objects.

#### Tagged Pointer Scheme

All values in Beerlang are represented as tagged 64-bit words. The lower 3 bits encode the type tag:

```
Pointer layout (64-bit):
┌─────────────────────────────────────────────────────┬─────────┐
│             61 bits (value/pointer)                 │ 3 bits  │
│                                                     │  (tag)  │
└─────────────────────────────────────────────────────┴─────────┘
```

**Tag Values:**
```
000 - Heap pointer (8-byte aligned, lower 3 bits naturally 0)
001 - Small integer (fixnum) - 61-bit signed integer
010 - Character - 32-bit Unicode codepoint
011 - Special constants (nil, true, false)
100 - (Reserved for future use)
101 - (Reserved for future use)
110 - (Reserved for future use)
111 - (Reserved for future use)
```

**Special Constants (tag 011):**
```
...0000011 - nil
...0001011 - false
...0010011 - true
```

#### Immediate Values (No Allocation)

**Fixnum (tag 001):**
- 61-bit signed integer stored directly in tagged word
- Range: -2^60 to 2^60-1
- No allocation, no refcounting needed
- Arithmetic may overflow to bigint

**Character (tag 010):**
- Unicode codepoint (up to 32 bits)
- Stored directly in tagged word
- No allocation needed

**Special Constants (tag 011):**
- `nil`, `true`, `false` encoded directly
- No allocation, recognized by bit pattern

#### Heap-Allocated Objects

All heap objects have a common header followed by type-specific data:

```c
struct Object {
    uint32_t type;      // Object type (8 bits type + 24 bits flags)
    uint32_t refcount;  // Reference count
    uint32_t size;      // Size/length (type-dependent meaning)
    void*    meta;      // Metadata pointer (or NULL)
    // Type-specific data follows...
};
```

**Object Header (16 bytes on 64-bit):**
- `type` (4 bytes): 8-bit type code + 24 bits for flags/subtype
- `refcount` (4 bytes): Reference count (32-bit, sufficient for practical use)
- `size` (4 bytes): Type-dependent (length, capacity, hash, etc.)
- `meta` (8 bytes): Optional metadata pointer (or NULL)

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
    Object    header;
    String*   name;       // Namespace name (e.g., "user", "clojure.core")
    HashMap*  vars;       // Map: simple-symbol -> Var
    HashMap*  aliases;    // Map: symbol -> Namespace (for 'require :as')
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
struct Cons {
    Object   header;
    Value    first;     // First element (car)
    Value    rest;      // Rest of list (cdr) or nil
};
```
- Classic cons cell for linked lists
- Immutable, structural sharing
- `rest` must be either another Cons or nil

**Vector (type 0x21):**
```c
struct Vector {
    Object   header;    // size = element count
    uint8_t  shift;     // Tree depth (5 bits per level)
    Value    tail[32];  // Last 32 elements (inline)
    void*    root;      // HAMT/RRB tree for earlier elements (or NULL)
};
```
- Persistent vector using HAMT or RRB-tree structure
- Last 32 elements in tail array for fast access
- O(log32 N) access and update
- Structural sharing on updates

**HashMap (type 0x22):**
```c
struct HashMap {
    Object   header;    // size = entry count
    void*    root;      // HAMT root node
};
```
- Persistent hash map using HAMT (Hash Array Mapped Trie)
- O(log32 N) lookup and update
- Structural sharing on updates
- Keys compared by value equality

**Function (type 0x30):**
```c
struct Function {
    Object   header;    // size = arity (or -1 for variadic)
    uint32_t code_idx;  // Index into code segment
    uint16_t n_closed;  // Number of closed-over values
    Value    closed[];  // Closed-over values (closure environment)
};
```
- Bytecode functions with optional closure
- `code_idx` points to bytecode in code segment
- `closed[]` captures lexical environment
- Arity stored in header for dispatch optimization

**Native Function (type 0x31):**
```c
struct NativeFunction {
    Object   header;    // size = arity (or -1 for variadic)
    void*    fn_ptr;    // C function pointer
    void*    data;      // Optional user data
};
```
- C functions callable from Beerlang
- Must follow calling convention (stack-based)

**Var (type 0x40):**
```c
struct Var {
    Object   header;
    Symbol*  name;      // Qualified symbol (has namespace)
    Value    value;     // Current value (unbound if special sentinel)
    uint8_t  dynamic;   // Thread-local binding flag
};
```
- Namespace-level named values
- Symbol must be qualified (has non-NULL namespace)
- Can be rebound (dynamic vars)
- Supports thread-local bindings
- Owned by the Namespace (stored in Namespace.vars map)

#### Namespace Registry and Symbol Resolution

**Global Namespace Registry:**
The VM maintains a global registry of all namespaces:

```c
// Global VM state
HashMap* namespace_registry;  // Map: String (name) -> Namespace
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

#### Persistent Data Structure Implementation

**Vectors and HashMaps use structural sharing:**

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
- Used for both vectors and hashmaps

#### Memory Layout Considerations

**Alignment:**
- All heap objects 8-byte aligned (allows tagged pointers)
- Header size: 16 bytes (good cache line utilization)
- Small objects (cons, closures) fit in single cache line (64 bytes)

**Cache Efficiency:**
- Small objects stay compact
- Persistent structures maximize sharing (less memory pressure)
- Reference counting keeps working set small

**Size Overhead:**
- 16-byte header (acceptable for persistent structures that share data)
- Immediate values (fixnum, char, bool, nil) have zero overhead
- Most programs dominated by collections with good data-to-header ratio

### Calling Convention

Beerlang uses a stack-based calling convention optimized for closures, tail calls, cooperative yielding, and reference counting.

#### Stack Layout

The VM maintains a single value stack for all operations. Each function call creates a **stack frame** with a specific layout:

```
High addresses (stack top)
┌─────────────────────────────┐
│   Local 0                   │  ← Frame base + n_args + 0
│   Local 1                   │  ← Frame base + n_args + 1
│   ...                       │
│   Local n-1                 │
├─────────────────────────────┤
│   Arg 0                     │  ← Frame base + 0
│   Arg 1                     │  ← Frame base + 1
│   ...                       │
│   Arg n-1                   │  ← Frame base + n_args - 1
├═════════════════════════════┤
│   Return Address (PC)       │  ← Frame base - 1
│   Previous Frame Pointer    │  ← Frame base - 2
│   Function Object           │  ← Frame base - 3 (for closure access)
│   Exception Handler         │  ← Frame base - 4 (or NULL)
└─────────────────────────────┘
Low addresses
```

**Stack Frame Structure:**
- **Arguments** (n values): Function arguments pushed by caller
- **Return Address**: Bytecode offset to return to
- **Previous Frame Pointer**: For stack unwinding
- **Function Object**: Pointer to Function object (for accessing closure environment)
- **Exception Handler**: Pointer to current try/catch handler (or NULL)
- **Locals** (n values): Space for `let` bindings and temporaries

#### Calling Sequence

**Caller (before CALL instruction):**
```
1. Push arguments left to right: arg0, arg1, ..., argN
2. Push function object to call
3. Execute CALL <n_args>
```

**CALL instruction (VM):**
```c
void execute_CALL(VM* vm, uint8_t n_args) {
    Value fn_val = pop(vm);

    // Check function type and arity
    if (!is_function(fn_val)) throw_error("Not a function");
    Function* fn = (Function*)untag_pointer(fn_val);

    if (fn->arity >= 0 && fn->arity != n_args) {
        throw_error("Arity mismatch");
    }

    // Build stack frame
    push(vm, make_handler_value(vm->exception_handler));
    push(vm, tag_pointer(fn));
    push(vm, make_int(vm->frame_pointer));
    push(vm, make_int(vm->pc));

    // Update frame pointer and PC
    vm->frame_pointer = vm->stack_pointer - 4 - n_args;
    vm->pc = fn->code_idx;

    // Allocate space for locals (determined by bytecode function)
    // ENTER <n_locals> instruction does this
}
```

**Function Entry (ENTER instruction):**
```
ENTER <n_locals>  ; First instruction in every function
- Allocates space for n_locals on stack
- Initializes them to nil
```

**Function Body:**
- Access arguments: `LOAD_LOCAL 0`, `LOAD_LOCAL 1`, etc.
- Access locals: `LOAD_LOCAL n_args+0`, `LOAD_LOCAL n_args+1`, etc.
- Access closure: `LOAD_CLOSURE 0`, `LOAD_CLOSURE 1`, etc.
  - These access the Function object at frame_base-3
  - Index into Function.closed[] array

**RETURN instruction:**
```c
void execute_RETURN(VM* vm) {
    Value return_value = pop(vm);

    // Get frame info
    int frame_base = vm->frame_pointer;
    Value pc_val = vm->stack[frame_base - 1];
    Value prev_fp_val = vm->stack[frame_base - 2];

    // Pop entire frame (args + locals + frame header)
    vm->stack_pointer = frame_base - 4;

    // Restore state
    vm->pc = untag_int(pc_val);
    vm->frame_pointer = untag_int(prev_fp_val);
    vm->exception_handler = untag_handler(vm->stack[frame_base - 4]);

    // Push return value
    push(vm, return_value);
}
```

#### Tail Call Optimization (Implicit)

Beerlang implements **proper tail calls** as a fundamental language feature. Tail call optimization is **implicit** - the compiler automatically detects when a call is in tail position and emits `TAIL_CALL` instead of `CALL`.

**Tail Position Definition:**

A call is in tail position when its return value is directly returned (no further computation):

```clojure
(fn [x]
  (if (< x 10)
    (foo x)         ; TAIL POSITION - result directly returned
    (bar x)))       ; TAIL POSITION - result directly returned

(fn [x]
  (do
    (println x)     ; NOT tail position
    (foo x)))       ; TAIL POSITION - last expression in do

(fn [x]
  (let [y (+ x 1)]
    (factorial y))) ; TAIL POSITION - last in let body

(fn [x]
  (+ 1 (foo x)))    ; NOT tail position - foo's result used by +
```

**Compiler Tail Position Detection:**

The compiler tracks tail position with a boolean flag during compilation:

```c
void compile_expr(Compiler* c, Expr* expr, bool in_tail_pos) {
    switch (expr->type) {
        case EXPR_CALL:
            // Compile arguments and function
            for (int i = 0; i < expr->n_args; i++) {
                compile_expr(c, expr->args[i], false);
            }
            compile_expr(c, expr->fn, false);

            // Emit appropriate call instruction
            if (in_tail_pos) {
                emit(c, OP_TAIL_CALL, expr->n_args);  // Automatic!
            } else {
                emit(c, OP_CALL, expr->n_args);
            }
            break;

        case EXPR_IF:
            compile_expr(c, expr->test, false);
            emit_jump_if_false(c, else_label);
            compile_expr(c, expr->then, in_tail_pos);  // Propagate tail pos
            emit_jump(c, end_label);
            emit_label(c, else_label);
            compile_expr(c, expr->else, in_tail_pos);  // Propagate tail pos
            emit_label(c, end_label);
            break;

        case EXPR_DO:
            for (int i = 0; i < expr->n_exprs - 1; i++) {
                compile_expr(c, expr->exprs[i], false);  // NOT tail
                emit(c, OP_POP);
            }
            // Last expression inherits tail position
            compile_expr(c, expr->exprs[expr->n_exprs - 1], in_tail_pos);
            break;

        case EXPR_LET:
            compile_bindings(c, expr->bindings);
            // Body inherits tail position
            compile_expr(c, expr->body, in_tail_pos);
            break;
    }
}

// Entry point: function body is always in tail position
void compile_function(Compiler* c, Fn* fn) {
    emit(c, OP_ENTER, count_locals(fn));
    compile_expr(c, fn->body, true);  // Body is in tail position!
    emit(c, OP_RETURN);
}
```

**TAIL_CALL Instruction Implementation:**

The `TAIL_CALL <n_args>` instruction reuses the current stack frame:

```c
void execute_TAIL_CALL(VM* vm, uint8_t n_args) {
    Value fn_val = pop(vm);

    // Save new arguments temporarily
    Value new_args[n_args];
    for (int i = n_args - 1; i >= 0; i--) {
        new_args[i] = pop(vm);
    }

    // Restore stack to current frame base (removing locals and old args)
    vm->stack_pointer = vm->frame_pointer;

    // Push new arguments
    for (int i = 0; i < n_args; i++) {
        push(vm, new_args[i]);
    }

    // Update function object and PC (keep same frame!)
    Function* fn = (Function*)untag_pointer(fn_val);
    vm->stack[vm->frame_pointer - 3] = fn_val;  // Update function object
    vm->pc = fn->code_idx;

    // ENTER instruction will allocate new locals
}
```

**Benefits of Implicit Tail Calls:**

1. **Natural functional style**: Write recursive code naturally without special syntax
2. **Better than Clojure's `recur`**:
   - Works for ALL tail calls, not just self-recursion
   - Mutual recursion works:
     ```clojure
     (fn even? [n]
       (if (= n 0) true (odd? (- n 1))))    ; Tail call to odd?

     (fn odd? [n]
       (if (= n 0) false (even? (- n 1))))  ; Tail call to even?
     ```
3. **No stack growth**: Bounded stack space for tail-recursive algorithms
4. **Proper tail calls**: Matches Scheme semantics
5. **REPL friendly**: Works automatically in REPL-compiled code
6. **Simple for users**: No need to learn special constructs

**Why This Works Well:**

- Tail position is well-defined in Lisp syntax
- Detection happens during compilation (REPL or file-based)
- No runtime overhead - just different bytecode instruction
- Clojure requires `recur` because JVM doesn't guarantee TCO
- Beerlang controls the VM, so we can do proper tail calls!

**Debug Mode:**

For debugging, a flag can disable tail call optimization to preserve stack traces:

```clojure
(set! *debug-mode* true)  ; Emit CALL even in tail position
```

This preserves full stack frames for better error messages during development.

#### Closure Access

Closures capture values from outer scopes:

```clojure
(let [x 10]
  (fn [y] (+ x y)))  ; x is captured
```

**Compiled representation:**
```
1. Evaluate outer binding: x = 10
2. Create Function object with n_closed=1
3. Function.closed[0] = value of x (captured)
4. LOAD_CLOSURE 0 in function body retrieves x
```

**LOAD_CLOSURE instruction:**
```c
void execute_LOAD_CLOSURE(VM* vm, uint8_t idx) {
    // Get function object from frame
    Value fn_val = vm->stack[vm->frame_pointer - 3];
    Function* fn = (Function*)untag_pointer(fn_val);

    // Load from closure environment
    push(vm, fn->closed[idx]);
}
```

**Note:** Closures capture **values**, not variables (immutability!). This works perfectly with persistent data structures and reference counting.

#### Cooperative Yielding

The `YIELD` instruction allows cooperative multitasking:

```c
void execute_YIELD(VM* vm) {
    // Save VM state (entire stack + registers is already in vm struct)
    vm->state = TASK_YIELDED;

    // Scheduler will resume by simply continuing execution
    // All state is preserved in stack frames
}
```

**Properties:**
- Can yield from any point in any function
- Stack frames preserve all state (args, locals, return addresses)
- Resume by restoring VM struct and continuing
- No special yield points needed - any basic block can yield

**Yield point insertion:**
The compiler automatically inserts YIELD instructions:
- At the start of loop iterations
- In long-running computations
- After N bytecode instructions (back-edge threshold)

#### Exception Handling

Each frame has an exception handler pointer:

**SETUP_TRY instruction:**
```c
void execute_SETUP_TRY(VM* vm, uint16_t catch_offset, uint16_t finally_offset) {
    // Create exception handler
    Handler* handler = create_handler(
        vm->frame_pointer,
        vm->pc + catch_offset,
        vm->pc + finally_offset,
        vm->exception_handler  // Link to previous handler
    );

    // Install as current handler
    vm->exception_handler = handler;
}
```

**THROW instruction:**
```c
void execute_THROW(VM* vm) {
    Value exception = pop(vm);

    // Find handler
    Handler* h = vm->exception_handler;
    if (h == NULL) {
        // Unhandled exception - terminate task
        vm->state = TASK_FAILED;
        return;
    }

    // Unwind stack to handler's frame
    vm->stack_pointer = h->frame_base;
    vm->frame_pointer = h->frame_base;

    // Jump to catch block
    vm->pc = h->catch_offset;

    // Push exception value for catch block
    push(vm, exception);

    // Restore previous handler
    vm->exception_handler = h->previous;
}
```

#### Reference Counting and Stack

**Stack references are NOT counted:**
- Values on the stack are owned by stack frames
- No inc/dec when pushing/popping to/from stack
- Only increment when storing to:
  - Heap objects (vectors, maps, closures)
  - Global vars
  - Locals that outlive the current frame (captured by closures)

**Frame cleanup:**
When a frame is popped (RETURN or exception), all stack values are implicitly released. The VM decrements refcounts for:
- Arguments (may be heap objects)
- Locals (may be heap objects)
- The function object itself

**Closure capture:**
When creating a closure that captures values:
```
MAKE_CLOSURE <code_idx> <n_closed>
- Pops n_closed values from stack
- Increments their refcounts (now owned by closure)
- Creates Function object with closed[] array
```

#### Native Function Calls

Native functions (C functions) must follow the same convention:

```c
typedef Value (*NativeFn)(VM* vm, int n_args);

Value my_native_add(VM* vm, int n_args) {
    // Arguments are on stack (top n_args values)
    // Must not modify stack below arguments

    if (n_args != 2) return make_error("Wrong arity");

    Value b = pop(vm);
    Value a = pop(vm);

    // Compute result
    Value result = add_values(a, b);

    // Push result (caller expects it on stack)
    push(vm, result);

    return result;  // Or return error value
}
```

**CALL_NATIVE instruction:**
- Similar to CALL but invokes C function pointer
- Arguments already on stack
- Native function pops args and pushes result
- No frame creation needed for simple natives
- Can optionally create frame for complex natives that need exception handling

#### Call Performance

**Optimizations:**
- **Direct calls**: When target is known at compile-time, can avoid var lookup
- **Inline caching**: Cache function objects at call sites
- **Monomorphic calls**: Specialize for single function at a call site
- **Arity checking**: Done once at compile-time for known arities
- **Stack allocation**: Fast (just increment pointer)

**Future JIT optimizations:**
- Inline small functions
- Remove frame overhead for leaf functions
- Register allocation for hot functions
- Devirtualize calls when target is statically known

### Cooperative Multitasking and Scheduler

Beerlang implements cooperative multitasking where lightweight tasks (green threads) are scheduled on a pool of OS threads. Tasks explicitly yield control or are automatically preempted at safe points.

#### Task Structure

A **Task** is a unit of concurrent execution with its own VM state:

```c
typedef enum {
    TASK_READY,       // Ready to run
    TASK_RUNNING,     // Currently executing
    TASK_YIELDED,     // Voluntarily yielded, ready to resume
    TASK_BLOCKED,     // Blocked on I/O or FFI call
    TASK_COMPLETED,   // Finished successfully
    TASK_FAILED       // Terminated with unhandled exception
} TaskState;

struct Task {
    // Unique identifier
    uint64_t       id;

    // Task state
    TaskState      state;

    // VM state (entire execution context)
    Value*         stack;           // Value stack
    int            stack_size;      // Allocated stack size
    int            stack_pointer;   // Current stack top
    int            frame_pointer;   // Current frame base
    uint32_t       pc;              // Program counter
    Handler*       exception_handler; // Exception handler chain

    // Scheduling
    uint64_t       time_slice;      // Instructions executed
    uint64_t       total_instructions; // Lifetime instruction count
    int            priority;        // Task priority (0-255)

    // Blocking state
    void*          blocked_on;      // Resource task is blocked on (or NULL)

    // Task-local storage
    HashMap*       thread_locals;   // Dynamic var bindings

    // Parent/child relationships (for task groups)
    Task*          parent;
    Vector*        children;

    // Result or error
    Value          result;          // Return value or exception
};
```

#### Scheduler Structure

The **Scheduler** manages all tasks and coordinates execution across OS threads:

```c
struct Scheduler {
    // Thread pool
    pthread_t*     threads;         // OS thread pool
    int            n_threads;       // Number of OS threads

    // Task queues (per priority level)
    Queue*         ready_queues[256]; // Ready tasks by priority
    Queue*         blocked_queue;   // Blocked tasks

    // Global state
    pthread_mutex_t lock;           // Protect scheduler state
    pthread_cond_t  work_available; // Signal threads when work available
    bool           shutdown;        // Shutdown flag

    // Statistics
    uint64_t       tasks_created;
    uint64_t       tasks_completed;
    uint64_t       context_switches;

    // Configuration
    uint32_t       time_slice_instructions; // Instructions per time slice
    bool           work_stealing_enabled;
};
```

#### Task Creation and Spawning

**spawn instruction or function:**

```clojure
;; Spawn a new task
(spawn (fn []
         (println "Running in separate task")
         (some-computation)))

;; Returns a task handle (future-like)
(let [task (spawn (fn [] (+ 1 2)))]
  (await task))  ; Block until task completes, get result
```

**Implementation:**

```c
// SPAWN bytecode instruction
void execute_SPAWN(VM* vm) {
    Value fn_val = pop(vm);

    // Create new task
    Task* task = task_create(
        (Function*)untag_pointer(fn_val),
        vm->current_ns,
        vm->current_task  // Parent task
    );

    // Add to scheduler
    scheduler_add_task(vm->scheduler, task);

    // Push task handle
    push(vm, make_task_handle(task));
}

Task* task_create(Function* fn, Namespace* ns, Task* parent) {
    Task* task = malloc(sizeof(Task));
    task->id = next_task_id();
    task->state = TASK_READY;
    task->priority = parent ? parent->priority : 128;  // Inherit priority

    // Allocate stack
    task->stack_size = 4096;  // Initial stack size
    task->stack = malloc(sizeof(Value) * task->stack_size);
    task->stack_pointer = 0;
    task->frame_pointer = 0;
    task->pc = fn->code_idx;

    // Setup initial frame for function call
    push_task(task, make_handler_value(NULL));
    push_task(task, tag_pointer(fn));
    push_task(task, make_int(0));  // No previous frame
    push_task(task, make_int(0));  // No return address
    task->frame_pointer = 0;

    // Thread-local storage (inherit from parent)
    task->thread_locals = parent ? copy_hashmap(parent->thread_locals)
                                 : hashmap_new();

    task->parent = parent;
    task->children = vector_new();
    if (parent) {
        vector_push(parent->children, task);
    }

    return task;
}
```

#### Scheduling Algorithm

**Round-robin with priorities:**

1. OS threads continuously execute tasks from ready queues
2. Higher priority tasks are scheduled first
3. Within same priority, round-robin
4. Time slice based on instruction count
5. Optional work stealing for load balancing

**Worker thread loop:**

```c
void* worker_thread(void* arg) {
    Scheduler* sched = (Scheduler*)arg;

    while (!sched->shutdown) {
        // Get next task to run
        Task* task = scheduler_get_next_task(sched);

        if (task == NULL) {
            // No work available, wait for signal
            pthread_mutex_lock(&sched->lock);
            pthread_cond_wait(&sched->work_available, &sched->lock);
            pthread_mutex_unlock(&sched->lock);
            continue;
        }

        // Execute task for one time slice
        task_run_time_slice(task, sched->time_slice_instructions);

        // Handle task state after execution
        switch (task->state) {
            case TASK_YIELDED:
            case TASK_READY:
                // Re-enqueue task
                scheduler_enqueue_task(sched, task);
                break;

            case TASK_BLOCKED:
                // Move to blocked queue
                scheduler_block_task(sched, task);
                break;

            case TASK_COMPLETED:
            case TASK_FAILED:
                // Cleanup and notify waiters
                scheduler_complete_task(sched, task);
                break;

            case TASK_RUNNING:
                // Should not happen
                assert(false);
                break;
        }

        sched->context_switches++;
    }

    return NULL;
}

Task* scheduler_get_next_task(Scheduler* sched) {
    pthread_mutex_lock(&sched->lock);

    // Check priority levels from high to low
    for (int pri = 255; pri >= 0; pri--) {
        if (!queue_empty(sched->ready_queues[pri])) {
            Task* task = queue_dequeue(sched->ready_queues[pri]);
            task->state = TASK_RUNNING;
            pthread_mutex_unlock(&sched->lock);
            return task;
        }
    }

    pthread_mutex_unlock(&sched->lock);
    return NULL;  // No tasks available
}
```

#### Task Execution (Time Slice)

```c
void task_run_time_slice(Task* task, uint32_t max_instructions) {
    uint32_t instructions_executed = 0;

    while (instructions_executed < max_instructions) {
        // Fetch and execute instruction
        uint8_t opcode = bytecode[task->pc];
        task->pc++;

        switch (opcode) {
            case OP_YIELD:
                // Explicit yield
                task->state = TASK_YIELDED;
                return;

            case OP_CALL_NATIVE:
                // Native FFI call - may block
                execute_call_native(task);
                if (task->state == TASK_BLOCKED) {
                    return;  // Task blocked on FFI
                }
                break;

            case OP_RETURN:
                execute_return(task);
                if (task->frame_pointer == 0) {
                    // Returned from top-level function
                    task->state = TASK_COMPLETED;
                    return;
                }
                break;

            // ... other instructions ...

            default:
                execute_instruction(task, opcode);
                break;
        }

        instructions_executed++;
        task->total_instructions++;

        // Check for unhandled exception
        if (task->state == TASK_FAILED) {
            return;
        }
    }

    // Time slice exhausted, yield
    task->state = TASK_YIELDED;
    task->time_slice += instructions_executed;
}
```

#### Automatic Yield Points

The compiler inserts automatic yield points to ensure responsiveness:

```c
void compile_loop(Compiler* c, Loop* loop) {
    emit_label(c, loop_start);

    // Compile loop condition and body
    compile_expr(c, loop->condition, false);
    emit_jump_if_false(c, loop_end);
    compile_expr(c, loop->body, false);

    // Automatic yield point at loop back-edge
    emit(c, OP_YIELD);

    emit_jump(c, loop_start);
    emit_label(c, loop_end);
}
```

**Benefits:**
- Long-running loops automatically yield
- No task can monopolize CPU
- Can be disabled in performance-critical code with annotations

#### FFI and Blocking Operations

When a task calls a native function that may block (I/O, system calls), the task is moved to a separate thread:

```c
void execute_call_native(Task* task) {
    Value fn_val = pop(task);
    NativeFunction* native = (NativeFunction*)untag_pointer(fn_val);

    if (native->blocking) {
        // Blocking call - dispatch to thread pool
        task->state = TASK_BLOCKED;
        task->blocked_on = native;

        // Submit to FFI thread pool
        ffi_thread_pool_submit(task, native);
    } else {
        // Non-blocking native call - execute inline
        Value result = native->fn_ptr(task, n_args);
        push(task, result);
    }
}

// FFI thread pool
void ffi_thread_pool_submit(Task* task, NativeFunction* fn) {
    FFIJob* job = malloc(sizeof(FFIJob));
    job->task = task;
    job->function = fn;

    // Submit to separate thread pool for blocking operations
    pthread_mutex_lock(&ffi_pool.lock);
    queue_enqueue(ffi_pool.job_queue, job);
    pthread_cond_signal(&ffi_pool.work_available);
    pthread_mutex_unlock(&ffi_pool.lock);
}

void* ffi_worker_thread(void* arg) {
    while (!shutdown) {
        FFIJob* job = ffi_pool_get_job();
        if (job == NULL) continue;

        // Execute blocking native function
        Value result = job->function->fn_ptr(job->task, n_args);

        // Store result and mark task as ready
        push(job->task, result);
        job->task->state = TASK_READY;
        job->task->blocked_on = NULL;

        // Re-enqueue task in scheduler
        scheduler_enqueue_task(scheduler, job->task);

        free(job);
    }
    return NULL;
}
```

#### Task Communication

**Channels for message passing:**

```clojure
;; Create channel
(def ch (chan 10))  ; Buffered channel with size 10

;; Task operations (yield if channel not ready)
(>! ch value)       ; Send to channel (yields task if full)
(let [val (<! ch)]  ; Receive from channel (yields task if empty)
  (println "Received:" val))

;; Blocking operations (block OS thread - use sparingly!)
(>!! ch value)      ; Blocking send (blocks OS thread if full)
(<!! ch)            ; Blocking receive (blocks OS thread if empty)
```

**Semantics (following Clojure core.async):**

- `<!` and `>!` - Cooperative operations inside tasks
  - May yield the current task if channel is not ready
  - Task is resumed when channel becomes ready
  - Preferred for use inside `spawn` tasks

- `<!!` and `>!!` - Thread-blocking operations
  - Block the OS thread (not recommended inside tasks!)
  - Useful for main thread or FFI context
  - Should be rare - tasks should use `<!` and `>!`

**Implementation:**

```c
struct Channel {
    Object     header;
    Queue*     buffer;        // Message buffer
    int        capacity;      // Buffer size (0 = unbuffered)
    Queue*     waiting_send;  // Tasks waiting to send
    Queue*     waiting_recv;  // Tasks waiting to receive
    pthread_mutex_t lock;
    bool       closed;
};

// >! - Cooperative send (yields task if channel full)
void channel_send_yielding(Task* task, Channel* ch, Value val) {
    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        throw_error(task, "Channel closed");
        return;
    }

    if (queue_size(ch->buffer) < ch->capacity) {
        // Buffer has space, enqueue immediately
        queue_enqueue(ch->buffer, val);

        // Wake up waiting receiver
        if (!queue_empty(ch->waiting_recv)) {
            Task* receiver = queue_dequeue(ch->waiting_recv);
            receiver->state = TASK_READY;
            scheduler_enqueue_task(scheduler, receiver);
        }

        pthread_mutex_unlock(&ch->lock);
    } else {
        // Buffer full, yield task (cooperative)
        task->state = TASK_BLOCKED;
        task->blocked_on = ch;
        queue_enqueue(ch->waiting_send, task);
        pthread_mutex_unlock(&ch->lock);
        // Task will be resumed when channel has space
    }
}

// >!! - Thread-blocking send (blocks OS thread if channel full)
bool channel_send_blocking(Channel* ch, Value val) {
    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }

    // Wait until buffer has space (blocks OS thread!)
    while (queue_size(ch->buffer) >= ch->capacity) {
        pthread_cond_wait(&ch->space_available, &ch->lock);
    }

    queue_enqueue(ch->buffer, val);

    // Wake up waiting receiver
    pthread_cond_signal(&ch->data_available);

    pthread_mutex_unlock(&ch->lock);
    return true;
}

// Similar implementations for <! and <!! (receive operations)
```

#### Work Stealing

Optional work-stealing for better load balancing:

```c
Task* scheduler_get_next_task_with_stealing(Scheduler* sched, int thread_id) {
    // Try local queue first
    Task* task = scheduler_get_next_task(sched);
    if (task != NULL) return task;

    // No local work, try stealing from other threads
    if (sched->work_stealing_enabled) {
        for (int i = 0; i < sched->n_threads; i++) {
            if (i == thread_id) continue;

            // Try to steal from thread i
            task = queue_steal(sched->ready_queues[i]);
            if (task != NULL) {
                sched->steals++;
                return task;
            }
        }
    }

    return NULL;
}
```

#### Task Lifecycle

```
CREATE → READY → RUNNING → YIELDED → RUNNING → ... → COMPLETED
                    ↓
                 BLOCKED → READY → RUNNING
                    ↓
                 FAILED
```

#### Performance Characteristics

**Lightweight tasks:**
- ~4KB initial stack per task
- Minimal per-task overhead (~200 bytes + stack)
- Can create thousands/millions of tasks
- Context switch is just saving/restoring VM state

**OS thread pool:**
- Default: Number of CPU cores
- Tasks multiplexed onto threads
- M:N threading model (M tasks on N threads)

**Scheduling overhead:**
- Lock-free queues where possible
- Per-priority queues reduce contention
- Work stealing improves load balancing

## Design Decisions

### Why Clojure Syntax?

- Proven, minimal, and elegant
- Great for macros and metaprogramming
- Minimal parser complexity

### Why Stack-based VM?

- Simple instruction set
- Cache-friendly when kept small
- Well-understood execution model

### Why Cooperative Multitasking?

- Predictable concurrency without preemption overhead
- Natural fit for VM-based execution
- Allows efficient scheduling of many lightweight tasks

### Why Cache-fit VM?

- Modern performance is dominated by memory access patterns
- L2/L3 cache residency provides dramatic speedup
- Small VM footprint enables this optimization

### Why Compile Everything (Including REPL)?

- Consistency: Same execution path for all code
- Performance: REPL code runs at full VM speed
- Simplicity: No separate interpreter to maintain
- Natural tail calls: Implicit optimization works everywhere

## Compiler

The compiler transforms S-expressions (produced by the reader) into bytecode. It handles macro expansion, special forms, lexical environments, closures, and optimizations like tail call detection.

### Compiler Structure

```c
struct Compiler {
    // Bytecode generation
    BytecodeBuffer*  bytecode;      // Output bytecode buffer
    ConstantPool*    constants;     // Constant pool

    // Environment
    LexicalEnv*      env;           // Current lexical environment
    Namespace*       current_ns;    // Current namespace

    // Compilation state
    bool             in_tail_pos;   // Currently in tail position?
    int              loop_depth;    // Nested loop depth
    Vector*          loop_labels;   // Loop continuation labels

    // Locals and closures
    Vector*          locals;        // Local variable names
    Vector*          captures;      // Captured variables (for closures)

    // Labels and jumps
    HashMap*         labels;        // Label name -> offset
    Vector*          unresolved_jumps; // Forward jumps to resolve

    // Error tracking
    const char*      filename;
    int              line;
    int              column;
};

// Lexical environment (for variable resolution)
struct LexicalEnv {
    LexicalEnv*      parent;        // Parent environment
    HashMap*         bindings;      // Name -> index/closure-index
    bool             is_function;   // Function boundary?
};

// Bytecode buffer (dynamic array)
struct BytecodeBuffer {
    uint8_t*         code;
    int              length;
    int              capacity;
};

// Constant pool
struct ConstantPool {
    Vector*          constants;     // Vector of constant values
    HashMap*         const_map;     // Value -> index (deduplication)
};
```

### Compilation Pipeline

```c
// Main entry point
BytecodeFunction* compile(Compiler* c, Value form) {
    // 1. Macro expansion
    Value expanded = macroexpand(c, form);

    // 2. Analyze form
    FormType type = analyze_form(expanded);

    // 3. Compile based on type
    switch (type) {
        case FORM_SPECIAL:
            compile_special_form(c, expanded);
            break;
        case FORM_CALL:
            compile_call(c, expanded);
            break;
        case FORM_LITERAL:
            compile_literal(c, expanded);
            break;
        case FORM_SYMBOL:
            compile_symbol(c, expanded);
            break;
    }

    // 4. Return bytecode function
    return finalize_bytecode(c);
}
```

### Macro Expansion

```c
Value macroexpand(Compiler* c, Value form) {
    while (is_macro_call(form)) {
        form = expand_macro_once(c, form);
    }
    return form;
}

bool is_macro_call(Value form) {
    if (!is_list(form)) return false;

    Value first = list_first(form);
    if (!is_symbol(first)) return false;

    Symbol* sym = (Symbol*)untag_pointer(first);
    Var* var = resolve_var(sym);

    return var != NULL && var_is_macro(var);
}

Value expand_macro_once(Compiler* c, Value form) {
    Symbol* macro_sym = (Symbol*)untag_pointer(list_first(form));
    Var* macro_var = resolve_var(macro_sym);
    Value macro_fn = var_get_value(macro_var);

    // Build argument list (rest of the form)
    Value args = list_rest(form);

    // Call macro function at compile time
    Value expanded = apply_function(macro_fn, args);

    return expanded;
}
```

### Special Form Compilation

```c
void compile_special_form(Compiler* c, Value form) {
    Symbol* special = (Symbol*)untag_pointer(list_first(form));
    const char* name = special->name;

    if (strcmp(name, "def") == 0) {
        compile_def(c, form);
    } else if (strcmp(name, "if") == 0) {
        compile_if(c, form);
    } else if (strcmp(name, "do") == 0) {
        compile_do(c, form);
    } else if (strcmp(name, "let") == 0) {
        compile_let(c, form);
    } else if (strcmp(name, "quote") == 0) {
        compile_quote(c, form);
    } else if (strcmp(name, "fn") == 0) {
        compile_fn(c, form);
    } else if (strcmp(name, "loop") == 0) {
        compile_loop(c, form);
    } else if (strcmp(name, "try") == 0) {
        compile_try(c, form);
    } else if (strcmp(name, "throw") == 0) {
        compile_throw(c, form);
    } else if (strcmp(name, "yield") == 0) {
        compile_yield(c, form);
    } else if (strcmp(name, "var") == 0) {
        compile_var(c, form);
    } else if (strcmp(name, ".") == 0) {
        compile_interop(c, form);
    } else if (strcmp(name, "disasm") == 0) {
        compile_disasm(c, form);
    } else if (strcmp(name, "asm") == 0) {
        compile_asm(c, form);
    } else {
        compile_error(c, "Unknown special form: %s", name);
    }
}
```

### Compiling Special Forms

#### def

```c
void compile_def(Compiler* c, Value form) {
    // (def name value)
    Value name = list_nth(form, 1);
    Value init = list_nth(form, 2);

    if (!is_symbol(name)) {
        compile_error(c, "def requires symbol as first argument");
        return;
    }

    Symbol* sym = (Symbol*)untag_pointer(name);

    // Compile init value
    compile_expr(c, init, false);

    // Create or update var in current namespace
    Var* var = intern_var(c->current_ns, sym);
    int var_idx = register_var(c, var);

    // Store to var
    emit(c, OP_STORE_VAR, var_idx);

    // Push var as result
    emit(c, OP_GET_VAR, var_idx);
}
```

#### if

```c
void compile_if(Compiler* c, Value form) {
    // (if test then else?)
    Value test = list_nth(form, 1);
    Value then_branch = list_nth(form, 2);
    Value else_branch = list_nth_or_nil(form, 3);

    // Compile test
    compile_expr(c, test, false);

    // Jump to else if false
    int else_label = make_label(c);
    emit_jump_if_false(c, else_label);

    // Compile then branch (inherits tail position)
    compile_expr(c, then_branch, c->in_tail_pos);

    // Jump over else
    int end_label = make_label(c);
    emit_jump(c, end_label);

    // Else branch
    bind_label(c, else_label);
    compile_expr(c, else_branch, c->in_tail_pos);

    bind_label(c, end_label);
}
```

#### do

```c
void compile_do(Compiler* c, Value form) {
    // (do expr1 expr2 ... exprN)
    Value exprs = list_rest(form);

    if (is_nil(exprs)) {
        // Empty do => nil
        emit(c, OP_PUSH_NIL);
        return;
    }

    // Compile all but last expression (not in tail position)
    while (!is_nil(list_rest(exprs))) {
        compile_expr(c, list_first(exprs), false);
        emit(c, OP_POP);  // Discard intermediate result
        exprs = list_rest(exprs);
    }

    // Last expression inherits tail position
    compile_expr(c, list_first(exprs), c->in_tail_pos);
}
```

#### let

```c
void compile_let(Compiler* c, Value form) {
    // (let [name1 val1 name2 val2 ...] body)
    Value bindings = list_nth(form, 1);
    Value body = list_nth(form, 2);

    if (!is_vector(bindings)) {
        compile_error(c, "let requires vector of bindings");
        return;
    }

    Vector* binding_vec = (Vector*)untag_pointer(bindings);
    if (binding_vec->header.size % 2 != 0) {
        compile_error(c, "let requires even number of forms in binding vector");
        return;
    }

    // Enter new lexical scope
    push_lexical_env(c);

    // Compile bindings
    for (int i = 0; i < binding_vec->header.size; i += 2) {
        Value name = vector_get(binding_vec, i);
        Value init = vector_get(binding_vec, i + 1);

        if (!is_symbol(name)) {
            compile_error(c, "let binding name must be symbol");
            return;
        }

        // Compile init expression
        compile_expr(c, init, false);

        // Bind to local
        int local_idx = add_local(c, (Symbol*)untag_pointer(name));
        emit(c, OP_STORE_LOCAL, local_idx);
    }

    // Compile body (inherits tail position)
    compile_expr(c, body, c->in_tail_pos);

    // Exit lexical scope
    pop_lexical_env(c);
}
```

#### fn

```c
void compile_fn(Compiler* c, Value form) {
    // (fn name? [params] body)
    // or (fn name? ([params1] body1) ([params2] body2) ...)

    Value rest = list_rest(form);

    // Check for optional name
    Symbol* fn_name = NULL;
    if (is_symbol(list_first(rest))) {
        fn_name = (Symbol*)untag_pointer(list_first(rest));
        rest = list_rest(rest);
    }

    // Single arity or multi-arity?
    if (is_vector(list_first(rest))) {
        // Single arity: (fn [params] body)
        compile_fn_arity(c, fn_name, list_first(rest), list_nth(rest, 1));
    } else {
        // Multi-arity: (fn ([params1] body1) ([params2] body2) ...)
        compile_fn_multi_arity(c, fn_name, rest);
    }
}

void compile_fn_arity(Compiler* c, Symbol* fn_name, Value params, Value body) {
    Vector* param_vec = (Vector*)untag_pointer(params);
    int arity = param_vec->header.size;

    // Create new compiler context for function body
    Compiler* fn_compiler = compiler_new_nested(c);

    // Enter function scope
    push_lexical_env(fn_compiler);
    fn_compiler->env->is_function = true;

    // Add parameters as locals
    for (int i = 0; i < arity; i++) {
        Value param = vector_get(param_vec, i);
        if (!is_symbol(param)) {
            compile_error(c, "Function parameter must be symbol");
            return;
        }
        add_local(fn_compiler, (Symbol*)untag_pointer(param));
    }

    // Add function name as local (for recursion)
    if (fn_name != NULL) {
        add_local(fn_compiler, fn_name);
    }

    // Emit ENTER instruction
    int n_locals = count_locals_in_body(body);
    emit(fn_compiler, OP_ENTER, n_locals);

    // Compile body in tail position
    fn_compiler->in_tail_pos = true;
    compile_expr(fn_compiler, body, true);

    // Emit RETURN
    emit(fn_compiler, OP_RETURN);

    // Finalize function bytecode
    BytecodeFunction* bc = finalize_bytecode(fn_compiler);

    // Create closure if needed
    if (fn_compiler->captures->size > 0) {
        // Push captured values onto stack
        for (int i = 0; i < fn_compiler->captures->size; i++) {
            Symbol* captured = vector_get(fn_compiler->captures, i);
            compile_symbol_load(c, captured);
        }

        // Create closure
        int code_idx = register_code(c, bc);
        emit(c, OP_MAKE_CLOSURE, code_idx, fn_compiler->captures->size);
    } else {
        // Simple function (no closure)
        int code_idx = register_code(c, bc);
        emit_load_constant(c, make_function(code_idx, arity));
    }

    pop_lexical_env(fn_compiler);
    compiler_free(fn_compiler);
}
```

#### loop/recur

```c
void compile_loop(Compiler* c, Value form) {
    // (loop [name1 val1 ...] body)
    Value bindings = list_nth(form, 1);
    Value body = list_nth(form, 2);

    Vector* binding_vec = (Vector*)untag_pointer(bindings);
    int n_bindings = binding_vec->header.size / 2;

    // Compile initial values
    for (int i = 0; i < n_bindings; i++) {
        Value init = vector_get(binding_vec, i * 2 + 1);
        compile_expr(c, init, false);
    }

    // Store to locals
    push_lexical_env(c);
    for (int i = 0; i < n_bindings; i++) {
        Value name = vector_get(binding_vec, i * 2);
        int local_idx = add_local(c, (Symbol*)untag_pointer(name));
        emit(c, OP_STORE_LOCAL, local_idx);
    }

    // Mark loop start
    int loop_start = current_offset(c);
    push_loop_label(c, loop_start);
    c->loop_depth++;

    // Compile body
    compile_expr(c, body, false);

    // Pop loop context
    c->loop_depth--;
    pop_loop_label(c);
    pop_lexical_env(c);
}

void compile_recur(Compiler* c, Value form) {
    // (recur arg1 arg2 ...)
    if (c->loop_depth == 0) {
        compile_error(c, "recur outside of loop");
        return;
    }

    Value args = list_rest(form);

    // Compile arguments
    int n_args = list_length(args);
    while (!is_nil(args)) {
        compile_expr(c, list_first(args), false);
        args = list_rest(args);
    }

    // Get loop start label
    int loop_start = peek_loop_label(c);

    // Jump back to loop (tail call optimization)
    emit_jump(c, loop_start);
}
```

### Function Calls

```c
void compile_call(Compiler* c, Value form) {
    Value fn_expr = list_first(form);
    Value args = list_rest(form);

    int n_args = list_length(args);

    // Compile arguments (left to right)
    Value arg_list = args;
    while (!is_nil(arg_list)) {
        compile_expr(c, list_first(arg_list), false);
        arg_list = list_rest(arg_list);
    }

    // Compile function expression
    compile_expr(c, fn_expr, false);

    // Emit call instruction (tail call if in tail position)
    if (c->in_tail_pos) {
        emit(c, OP_TAIL_CALL, n_args);
    } else {
        emit(c, OP_CALL, n_args);
    }
}
```

### Symbol Resolution

```c
void compile_symbol(Compiler* c, Value sym_val) {
    Symbol* sym = (Symbol*)untag_pointer(sym_val);

    // 1. Check for local binding
    int local_idx = lookup_local(c, sym);
    if (local_idx >= 0) {
        emit(c, OP_LOAD_LOCAL, local_idx);
        return;
    }

    // 2. Check for closure capture
    int closure_idx = lookup_closure(c, sym);
    if (closure_idx >= 0) {
        emit(c, OP_LOAD_CLOSURE, closure_idx);
        return;
    }

    // 3. Check if we need to capture from parent scope
    if (should_capture(c, sym)) {
        closure_idx = add_capture(c, sym);
        emit(c, OP_LOAD_CLOSURE, closure_idx);
        return;
    }

    // 4. Resolve as namespace var
    Var* var = resolve_var_compile_time(c, sym);
    if (var == NULL) {
        compile_error(c, "Unable to resolve symbol: %s", sym->name);
        return;
    }

    int var_idx = register_var(c, var);
    emit(c, OP_LOAD_VAR, var_idx);
}

bool should_capture(Compiler* c, Symbol* sym) {
    // Walk up lexical environments looking for binding
    LexicalEnv* env = c->env->parent;
    while (env != NULL) {
        if (hashmap_contains(env->bindings, sym)) {
            // Found in parent scope
            if (env->is_function) {
                // Crossed function boundary - need to capture
                return true;
            }
            // Same function, just outer let - use local
            return false;
        }
        env = env->parent;
    }
    return false;
}
```

### Literal Compilation

```c
void compile_literal(Compiler* c, Value literal) {
    if (is_nil(literal)) {
        emit(c, OP_PUSH_NIL);
    } else if (is_true(literal)) {
        emit(c, OP_PUSH_TRUE);
    } else if (is_false(literal)) {
        emit(c, OP_PUSH_FALSE);
    } else if (is_fixnum(literal)) {
        long long n = untag_fixnum(literal);
        if (n >= -128 && n <= 127) {
            emit(c, OP_PUSH_INT, (int8_t)n);
        } else {
            int idx = add_constant(c, literal);
            emit(c, OP_PUSH_CONST, idx);
        }
    } else {
        // Other literals go in constant pool
        int idx = add_constant(c, literal);
        emit(c, OP_PUSH_CONST, idx);
    }
}
```

### Optimizations

#### Constant Folding

```c
Value constant_fold(Value form) {
    if (!is_list(form)) return form;

    Value fn = list_first(form);
    if (!is_symbol(fn)) return form;

    Symbol* fn_sym = (Symbol*)untag_pointer(fn);

    // Check if all arguments are constants
    Value args = list_rest(form);
    if (!all_constants(args)) return form;

    // Fold known operations
    if (strcmp(fn_sym->name, "+") == 0) {
        return fold_add(args);
    } else if (strcmp(fn_sym->name, "*") == 0) {
        return fold_mul(args);
    } else if (strcmp(fn_sym->name, "-") == 0) {
        return fold_sub(args);
    }
    // etc.

    return form;
}
```

#### Dead Code Elimination

```c
void eliminate_dead_code(Compiler* c) {
    // Remove code after unconditional jumps/returns
    BytecodeBuffer* bc = c->bytecode;

    for (int i = 0; i < bc->length; i++) {
        uint8_t op = bc->code[i];

        if (op == OP_RETURN || op == OP_JUMP) {
            // Skip to next label or end
            i++;
            while (i < bc->length && !is_label_target(c, i)) {
                bc->code[i] = OP_NOP;  // Replace with NOP
                i++;
            }
        }
    }
}
```

### Error Reporting

```c
void compile_error(Compiler* c, const char* fmt, ...) {
    fprintf(stderr, "%s:%d:%d: compilation error: ",
            c->filename, c->line, c->column);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");

    c->error = true;
}
```

### Bytecode Emission

```c
void emit(Compiler* c, uint8_t opcode, ...) {
    append_byte(c->bytecode, opcode);

    // Handle operands based on instruction
    va_list args;
    va_start(args, opcode);

    switch (opcode) {
        case OP_PUSH_INT:
            append_byte(c->bytecode, va_arg(args, int));
            break;
        case OP_LOAD_VAR:
        case OP_STORE_VAR:
        case OP_LOAD_LOCAL:
        case OP_STORE_LOCAL:
        case OP_LOAD_CLOSURE:
        case OP_PUSH_CONST:
        case OP_CALL:
        case OP_TAIL_CALL:
        case OP_ENTER:
            append_byte(c->bytecode, va_arg(args, int));
            break;
        case OP_MAKE_CLOSURE:
            append_byte(c->bytecode, va_arg(args, int));  // code_idx
            append_byte(c->bytecode, va_arg(args, int));  // n_captures
            break;
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
            append_short(c->bytecode, va_arg(args, int));
            break;
        // ... other instructions
    }

    va_end(args);
}

void emit_jump(Compiler* c, int label) {
    if (is_label_bound(c, label)) {
        // Label already bound - emit resolved jump
        int offset = get_label_offset(c, label) - current_offset(c);
        emit(c, OP_JUMP, offset);
    } else {
        // Forward reference - record for later resolution
        int jump_pos = current_offset(c);
        emit(c, OP_JUMP, 0);  // Placeholder
        record_unresolved_jump(c, label, jump_pos);
    }
}

void bind_label(Compiler* c, int label) {
    set_label_offset(c, label, current_offset(c));
    resolve_jumps_to_label(c, label);
}
```

### Compilation Example

```clojure
;; Source
(defn factorial [n]
  (if (< n 2)
    1
    (* n (factorial (- n 1)))))

;; Compilation process:
;; 1. Macroexpand: defn -> def + fn
;; 2. Compile def:
;;    - Compile fn body
;;    - Store to var
;; 3. Compile fn:
;;    - Create function context
;;    - Compile if (tail position)
;;    - Compile recursive call (tail call!)
;; 4. Emit bytecode

;; Generated bytecode:
;; Function factorial:
;;   ENTER 0
;;   LOAD_VAR <n-var>
;;   PUSH_INT 2
;;   LT
;;   JUMP_IF_FALSE :else
;;   PUSH_INT 1
;;   RETURN
;; :else
;;   LOAD_VAR <*-var>
;;   LOAD_LOCAL 0
;;   LOAD_VAR <factorial-var>
;;   LOAD_VAR <--var>
;;   LOAD_LOCAL 0
;;   PUSH_INT 1
;;   CALL 2
;;   TAIL_CALL 1     ; Tail call!
```

### Future Optimizations

1. **Inline small functions** at call sites
2. **Specialize polymorphic operations** (type-specific math)
3. **Register allocation** for hot functions
4. **Loop unrolling** for known iteration counts
5. **Escape analysis** for stack allocation
6. **Profile-guided optimization** based on runtime data

## REPL Evaluation Model

Beerlang's REPL follows a **compile-everything** approach: all code, including simple expressions typed at the REPL, is compiled to bytecode before execution.

### REPL Execution Flow

```
Read → Compile → Execute → Print
```

**Simple expression:**
```clojure
user=> (+ 1 2)
```

Internally:
1. **Read**: Parse `(+ 1 2)`
2. **Compile** to bytecode:
   ```
   LOAD_VAR <idx-of-+>
   PUSH_INT 1
   PUSH_INT 2
   CALL 2
   RETURN
   ```
3. **Execute**: Run bytecode on VM
4. **Print**: `3`

**Function definition:**
```clojure
user=> (defn square [x] (* x x))
```

Internally:
1. **Read**: Parse entire `defn` form
2. **Expand macro**: `defn` → `def` + `fn`
3. **Compile**:
   - Compile `(fn [x] (* x x))` to bytecode function
   - Generate code to create Function object
   - Generate code to store in namespace var
4. **Execute**: Create function, bind to `user/square`
5. **Print**: `#'user/square`

**Interactive code:**
```clojure
user=> (do
         (println "Hello")
         (let [x 10]
           (* x x)))
```

Internally:
1. **Read**: Parse entire expression
2. **Compile** to bytecode:
   ```
   LOAD_VAR <println>
   PUSH_CONST <"Hello">
   CALL 1
   POP
   PUSH_INT 10
   STORE_LOCAL 0
   LOAD_LOCAL 0
   LOAD_LOCAL 0
   CALL <*> 2
   RETURN
   ```
3. **Execute**: Run bytecode
4. **Print**: `100` (after printing "Hello")

### Benefits of Compile-Everything Approach

1. **Consistency**: Same semantics in REPL and compiled files
   - Tail calls work the same way
   - Same performance characteristics
   - Same error messages

2. **Performance**: No interpretation overhead
   - Even REPL code runs at VM speed
   - Bytecode is cached and reusable
   - No mode switch between REPL and "real" code

3. **Simplicity**: Single code path
   - No separate interpreter
   - Smaller VM implementation
   - Easier to reason about and test

4. **Incremental compilation**: Each form is independently compiled
   - Fast feedback cycle
   - Can redefine functions interactively
   - No need to reload entire files

5. **Natural tail calls**:
   - Compiler detects tail position during REPL compilation
   - Works exactly the same as in file-based code
   - No special REPL mode needed

### REPL Implementation

```c
void repl_eval(VM* vm, const char* input) {
    // 1. Read
    Expr* expr = parse(input);
    if (expr == NULL) {
        print_parse_error();
        return;
    }

    // 2. Compile
    Compiler* c = compiler_new();
    BytecodeFunction* bc = compile_expr_to_function(c, expr, vm->current_ns);
    if (bc == NULL) {
        print_compile_error(c);
        return;
    }

    // 3. Execute
    // Create temporary function for this REPL form
    Function* fn = create_function(bc, 0);  // 0 arity, no args

    // Call it
    push(vm, tag_pointer(fn));
    execute_CALL(vm, 0);
    vm_run(vm);  // Execute until RETURN

    // 4. Print
    if (vm->state == TASK_COMPLETED) {
        Value result = pop(vm);
        print_value(result);
    } else {
        print_error(vm);
    }
}
```

### Does This Affect Design Principles?

**No - it reinforces them:**

- ✅ **Cache-efficient**: Bytecode is compact, fits in cache
- ✅ **Cooperative multitasking**: Works identically in REPL
- ✅ **Tail calls**: Implicit optimization during compilation
- ✅ **Reference counting**: Same GC in REPL and compiled code
- ✅ **Test-driven**: REPL code can be tested like any code
- ✅ **Simpler**: No interpreter → smaller VM → more cache-friendly

The compile-everything approach actually makes the system **simpler and more consistent** rather than more complex. It's one unified path from source to execution.

## Input/Output System

Beerlang's I/O system is designed to work seamlessly with cooperative multitasking. All I/O operations are non-blocking at the task level, allowing tasks to perform I/O without blocking other tasks.

### Core Philosophy

1. **Non-blocking for tasks**: I/O operations yield tasks, not block threads
2. **Event-driven**: Use OS-level async I/O (epoll, kqueue, IOCP)
3. **Stream abstraction**: Unified interface for files, sockets, stdin/out
4. **Buffered by default**: Automatic buffering for performance
5. **Integration with scheduler**: I/O tasks managed by scheduler

### I/O Architecture

```
┌─────────────────────────────────────────┐
│         Beerlang Tasks                  │
│  (read, write, println, etc.)           │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│         Stream Interface                │
│  (File, Socket, Stdin/out, Buffer)      │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│         I/O Scheduler                   │
│  (Event loop, completion handlers)      │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│   OS Async I/O (epoll/kqueue/IOCP)     │
│   or I/O Thread Pool (for blocking)    │
└─────────────────────────────────────────┘
```

### Stream Abstraction

```c
typedef enum {
    STREAM_FILE,
    STREAM_SOCKET,
    STREAM_BUFFER,
    STREAM_STDIN,
    STREAM_STDOUT,
    STREAM_STDERR,
    STREAM_PIPE,
    STREAM_CHANNEL
} StreamType;

struct Stream {
    Object         header;
    StreamType     type;
    int            fd;              // File descriptor (if applicable)
    bool           readable;
    bool           writable;
    bool           closed;

    // Buffering
    Buffer*        read_buffer;
    Buffer*        write_buffer;

    // Async state
    bool           read_pending;
    bool           write_pending;
    Queue*         waiting_readers;  // Tasks waiting to read
    Queue*         waiting_writers;  // Tasks waiting to write

    // Stream-specific data
    void*          stream_data;

    // Encoding (for text streams)
    Encoding       encoding;        // UTF-8, ASCII, etc.
};

struct Buffer {
    uint8_t*       data;
    int            size;
    int            capacity;
    int            read_pos;
    int            write_pos;
};
```

### I/O Operations

#### Reading

```clojure
;; Read operations (yield task if data not available)
(read stream)              ; Read one form (for text)
(read-line stream)         ; Read line as string
(read-bytes stream n)      ; Read n bytes
(read-string stream)       ; Read all as string
(read-char stream)         ; Read single character

;; Non-blocking variants (return nil if no data)
(read-available stream)    ; Read whatever is available
```

**Implementation:**

```c
// Read from stream (cooperative)
Value stream_read(Task* task, Stream* stream, int n_bytes) {
    // Check if stream is readable
    if (!stream->readable) {
        return make_error("Stream not readable");
    }

    if (stream->closed) {
        return make_eof();
    }

    // Try to read from buffer first
    if (buffer_available(stream->read_buffer) >= n_bytes) {
        return buffer_read(stream->read_buffer, n_bytes);
    }

    // Need more data - check if async read is pending
    if (!stream->read_pending) {
        // Initiate async read
        initiate_async_read(stream);
        stream->read_pending = true;
    }

    // Block task until data available
    task->state = TASK_BLOCKED;
    task->blocked_on = stream;
    queue_enqueue(stream->waiting_readers, task);

    // Task will be resumed by I/O scheduler when data arrives
    return YIELD_VALUE;  // Special value indicating task should yield
}
```

#### Writing

```clojure
;; Write operations (yield task if buffer full)
(write stream data)        ; Write data (string, bytes, or value)
(write-line stream s)      ; Write string + newline
(write-bytes stream bytes) ; Write byte array
(flush stream)             ; Flush write buffer

;; Formatted output
(printf stream fmt & args) ; Formatted output
(println & args)           ; Write to stdout + newline
(prn & args)               ; Write readable form + newline
```

**Implementation:**

```c
// Write to stream (cooperative)
Value stream_write(Task* task, Stream* stream, Value data) {
    if (!stream->writable) {
        return make_error("Stream not writable");
    }

    if (stream->closed) {
        return make_error("Stream closed");
    }

    // Convert data to bytes
    ByteArray* bytes = value_to_bytes(data);

    // Try to write to buffer
    if (buffer_space_available(stream->write_buffer) >= bytes->size) {
        buffer_write(stream->write_buffer, bytes->data, bytes->size);

        // Initiate async flush if needed
        if (!stream->write_pending) {
            initiate_async_write(stream);
            stream->write_pending = true;
        }

        return make_nil();
    }

    // Buffer full - block task
    task->state = TASK_BLOCKED;
    task->blocked_on = stream;
    queue_enqueue(stream->waiting_writers, task);

    return YIELD_VALUE;
}
```

### I/O Scheduler (Reactor Pattern)

The I/O scheduler implements the **Reactor Pattern** with a small number of dedicated I/O threads running event loops. This is the classic approach for high-performance async I/O.

#### Thread Architecture

```
┌─────────────────────────────────────────────────┐
│  Worker Threads (N = CPU cores)                 │
│  - Execute beerlang tasks                       │
│  - Initiate I/O operations                      │
│  - Block tasks when I/O not ready               │
└────────────────┬────────────────────────────────┘
                 │
                 │ (I/O requests)
                 ▼
┌─────────────────────────────────────────────────┐
│  Reactor Threads (1-2 threads)                  │
│  - Run event loop (epoll_wait/kqueue)           │
│  - Detect I/O readiness                         │
│  - Wake blocked tasks                           │
└────────────────┬────────────────────────────────┘
                 │
                 │ (OS events)
                 ▼
┌─────────────────────────────────────────────────┐
│  OS Kernel (epoll/kqueue/IOCP)                  │
│  - Monitors file descriptors                    │
│  - Notifies when I/O ready                      │
└─────────────────────────────────────────────────┘
```

**Why 1-2 reactor threads?**
- Single reactor thread handles thousands of connections
- Second reactor for redundancy/load distribution (optional)
- More threads don't help - reactor is event-driven, not CPU-bound
- Keeps system simple and cache-friendly

#### Reactor Components

```c
struct IOScheduler {
    // Reactor threads
    pthread_t        reactor_threads[2];  // 1-2 reactor threads
    int              n_reactors;

    // Event demultiplexer (per reactor)
    int              epoll_fd[2];         // Linux: epoll
    // int           kqueue_fd[2];        // BSD/macOS: kqueue
    // HANDLE        iocp[2];             // Windows: IOCP

    // Registered streams (shared across reactors)
    HashMap*         streams;             // fd -> Stream
    pthread_rwlock_t streams_lock;

    // I/O thread pool (for truly blocking operations like disk I/O)
    ThreadPool*      io_thread_pool;

    // Completion queue (reactor -> scheduler communication)
    Queue*           completions;
    pthread_mutex_t  completion_lock;

    // Shutdown flag
    bool             shutdown;
};
```

#### Reactor Thread Event Loop

```c
void* reactor_thread_main(void* arg) {
    IOScheduler* ios = (IOScheduler*)arg;
    int reactor_id = /* assigned reactor ID */;
    int epoll_fd = ios->epoll_fd[reactor_id];

    struct epoll_event events[MAX_EVENTS];

    while (!ios->shutdown) {
        // Wait for I/O events (with timeout for shutdown checks)
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            // Look up stream (read-lock for concurrent access)
            pthread_rwlock_rdlock(&ios->streams_lock);
            Stream* stream = hashmap_get(ios->streams, fd);
            pthread_rwlock_unlock(&ios->streams_lock);

            if (stream == NULL) continue;

            // Handle I/O events
            if (events[i].events & EPOLLIN) {
                // Data available for reading
                handle_read_ready(ios, stream);
            }

            if (events[i].events & EPOLLOUT) {
                // Ready for writing
                handle_write_ready(ios, stream);
            }

            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                // Error or hangup
                handle_stream_error(ios, stream);
            }
        }

        // Process completions from I/O thread pool
        process_io_completions(ios);
    }

    return NULL;
}
```

#### Reactor Initialization

```c
IOScheduler* io_scheduler_init(int n_reactors) {
    IOScheduler* ios = malloc(sizeof(IOScheduler));

    // Default to 1 reactor thread (sufficient for most workloads)
    ios->n_reactors = (n_reactors > 0) ? n_reactors : 1;
    if (ios->n_reactors > 2) ios->n_reactors = 2;  // Max 2

    // Initialize event demultiplexers
    for (int i = 0; i < ios->n_reactors; i++) {
        #ifdef __linux__
        ios->epoll_fd[i] = epoll_create1(0);
        #elif defined(__APPLE__) || defined(__FreeBSD__)
        ios->kqueue_fd[i] = kqueue();
        #endif
    }

    // Initialize data structures
    ios->streams = hashmap_new();
    pthread_rwlock_init(&ios->streams_lock, NULL);
    ios->completions = queue_new();
    pthread_mutex_init(&ios->completion_lock, NULL);

    // Create I/O thread pool (for blocking operations)
    ios->io_thread_pool = thread_pool_create(4);  // 4 threads for blocking I/O

    // Start reactor threads
    for (int i = 0; i < ios->n_reactors; i++) {
        pthread_create(&ios->reactor_threads[i], NULL,
                      reactor_thread_main, ios);
    }

    return ios;
}
```

#### Event Handler (Read Ready)

```c
void handle_read_ready(IOScheduler* ios, Stream* stream) {
    // Read data into buffer (non-blocking)
    uint8_t temp_buf[8192];
    ssize_t n = read(stream->fd, temp_buf, sizeof(temp_buf));

    if (n > 0) {
        // Append to read buffer
        buffer_append(stream->read_buffer, temp_buf, n);
        stream->read_pending = false;

        // Wake up waiting readers (add to completion queue)
        while (!queue_empty(stream->waiting_readers)) {
            Task* task = queue_dequeue(stream->waiting_readers);

            // Signal main scheduler (thread-safe)
            pthread_mutex_lock(&ios->completion_lock);
            queue_enqueue(ios->completions, task);
            pthread_mutex_unlock(&ios->completion_lock);
        }
    } else if (n == 0) {
        // EOF
        stream->closed = true;
        wake_all_waiting(ios, stream);
    } else {
        // Error (EAGAIN/EWOULDBLOCK is normal for non-blocking)
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            handle_read_error(stream, errno);
        }
    }
}
```

#### Registering Streams with Reactor

```c
void io_scheduler_register(IOScheduler* ios, Stream* stream) {
    // Choose reactor (round-robin)
    int reactor_id = stream->fd % ios->n_reactors;
    int epoll_fd = ios->epoll_fd[reactor_id];

    // Register with epoll for read/write events
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;  // Edge-triggered
    ev.data.fd = stream->fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, stream->fd, &ev);

    // Add to streams map
    pthread_rwlock_wrlock(&ios->streams_lock);
    hashmap_put(ios->streams, stream->fd, stream);
    pthread_rwlock_unlock(&ios->streams_lock);
}
```

#### Integration with Main Scheduler

The main task scheduler periodically checks for I/O completions:

```c
void scheduler_check_io_completions(Scheduler* sched) {
    IOScheduler* ios = sched->io_scheduler;

    pthread_mutex_lock(&ios->completion_lock);

    // Move all completed I/O tasks to ready queue
    while (!queue_empty(ios->completions)) {
        Task* task = queue_dequeue(ios->completions);
        task->state = TASK_READY;
        task->blocked_on = NULL;
        scheduler_enqueue_task(sched, task);
    }

    pthread_mutex_unlock(&ios->completion_lock);
}
```

#### Why This Design Works

1. **Separation of concerns:**
   - Worker threads: Run beerlang tasks (CPU work)
   - Reactor threads: Handle I/O events (I/O work)
   - Clear boundary between computation and I/O

2. **Scalability:**
   - N worker threads for N CPU cores
   - 1-2 reactor threads handle thousands of connections
   - I/O thread pool for blocking operations (disk I/O)

3. **Cache-friendly:**
   - Reactor threads have tight event loop
   - Stay in cache, high performance
   - Worker threads execute bytecode (also cache-friendly)

4. **Classic proven pattern:**
   - Used by nginx, Node.js, Redis
   - Well-understood performance characteristics
   - Easy to reason about and debug

void handle_read_ready(Stream* stream) {
    // Read data into buffer
    uint8_t temp_buf[8192];
    ssize_t n = read(stream->fd, temp_buf, sizeof(temp_buf));

    if (n > 0) {
        // Append to read buffer
        buffer_append(stream->read_buffer, temp_buf, n);
        stream->read_pending = false;

        // Wake up waiting readers
        while (!queue_empty(stream->waiting_readers)) {
            Task* task = queue_dequeue(stream->waiting_readers);
            task->state = TASK_READY;
            task->blocked_on = NULL;
            scheduler_enqueue_task(scheduler, task);
        }
    } else if (n == 0) {
        // EOF
        stream->closed = true;
        wake_all_waiting(stream);
    } else {
        // Error
        handle_read_error(stream, errno);
    }
}
```

### Standard Streams

Standard streams (stdin, stdout, stderr) have special handling, especially in REPL:

```c
// Global standard streams
Stream* STDIN;
Stream* STDOUT;
Stream* STDERR;

// Dynamic vars for current streams (can be rebound)
Var* VAR_IN;    // *in*
Var* VAR_OUT;   // *out*
Var* VAR_ERR;   // *err*

void init_standard_streams() {
    STDIN = stream_from_fd(STDIN_FILENO, true, false);
    STDOUT = stream_from_fd(STDOUT_FILENO, false, true);
    STDERR = stream_from_fd(STDERR_FILENO, false, true);

    // Make stdout/stderr line-buffered
    stream_set_line_buffered(STDOUT, true);
    stream_set_line_buffered(STDERR, true);

    // Bind dynamic vars
    VAR_IN = intern_var_with_value(intern_symbol("*in*"),
                                    make_stream(STDIN));
    VAR_OUT = intern_var_with_value(intern_symbol("*out*"),
                                     make_stream(STDOUT));
    VAR_ERR = intern_var_with_value(intern_symbol("*err*"),
                                     make_stream(STDERR));

    // Make them dynamic (thread-local)
    var_set_dynamic(VAR_IN, true);
    var_set_dynamic(VAR_OUT, true);
    var_set_dynamic(VAR_ERR, true);
}
```

**Using standard streams:**

```clojure
;; Read from stdin
(read-line *in*)

;; Write to stdout
(println *out* "Hello, world!")

;; Or use convenience functions
(println "Hello!")     ; Uses *out* implicitly
(read-line)            ; Uses *in* implicitly

;; Rebind streams
(binding [*out* (open "output.txt")]
  (println "This goes to file"))

;; Capture output
(with-out-str
  (println "captured"))
```

### File I/O

```clojure
;; Open file
(def f (open "file.txt" :read))      ; Read-only
(def f (open "file.txt" :write))     ; Write (truncate)
(def f (open "file.txt" :append))    ; Append
(def f (open "file.txt" :read-write)) ; Read-write

;; Read from file
(read-line f)
(read-string f)       ; Read entire file

;; Write to file
(write f "data")
(write-line f "line")

;; Close file
(close f)

;; With resource management
(with-open [f (open "file.txt" :read)]
  (read-string f))    ; Automatically closed

;; Slurp/spit helpers
(slurp "file.txt")    ; Read entire file as string
(spit "file.txt" data) ; Write data to file
```

**Implementation:**

```c
Stream* open_file(const char* path, OpenMode mode) {
    int flags = 0;
    bool readable = false;
    bool writable = false;

    switch (mode) {
        case MODE_READ:
            flags = O_RDONLY;
            readable = true;
            break;
        case MODE_WRITE:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            writable = true;
            break;
        case MODE_APPEND:
            flags = O_WRONLY | O_CREAT | O_APPEND;
            writable = true;
            break;
        case MODE_READ_WRITE:
            flags = O_RDWR | O_CREAT;
            readable = true;
            writable = true;
            break;
    }

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        return NULL;  // Error
    }

    // Make non-blocking
    fcntl(fd, F_SETFL, O_NONBLOCK);

    Stream* stream = stream_from_fd(fd, readable, writable);
    stream->type = STREAM_FILE;

    // Register with I/O scheduler
    io_scheduler_register(io_scheduler, stream);

    return stream;
}
```

### Network I/O

```clojure
;; TCP client
(def sock (connect "example.com" 80))
(write sock "GET / HTTP/1.0\r\n\r\n")
(flush sock)
(def response (read-string sock))
(close sock)

;; TCP server
(def server (listen 8080))
(loop []
  (let [client (accept server)]
    (spawn (handle-client client)))
  (recur))

;; UDP
(def udp-sock (udp-socket))
(bind udp-sock 8080)
(def [data addr] (recv-from udp-sock))
(send-to udp-sock data addr)
```

### Buffering Strategies

```clojure
;; No buffering
(set-buffering stream :none)

;; Line buffering (default for terminals)
(set-buffering stream :line)

;; Full buffering with size
(set-buffering stream :full 8192)
```

### Async I/O Patterns

```clojure
;; Read asynchronously
(spawn
  (loop []
    (when-let [line (read-line stream)]
      (process-line line)
      (recur))))

;; Multiple streams with select
(loop []
  (let [[ready _] (select [stream1 stream2 stream3] 1000)]
    (doseq [s ready]
      (process-stream s)))
  (recur))

;; Timeout on read
(with-timeout 5000
  (read-line stream))  ; Throws timeout exception

;; Parallel reads
(let [task1 (spawn (read-file "file1.txt"))
      task2 (spawn (read-file "file2.txt"))]
  [(await task1) (await task2)])
```

### REPL-Specific I/O

In the REPL, stdin/stdout require special handling:

```c
void repl_init_io() {
    // stdin in REPL should be blocking for the main thread
    // But other tasks can do non-blocking I/O

    // Create line-reading interface for REPL
    REPL_STDIN = create_repl_stdin();

    // Stdout/stderr remain non-blocking
    // But REPL ensures they're flushed before prompting
}

String* repl_read_line() {
    // Blocking read for REPL (main thread only)
    // Uses readline or similar for editing
    char* line = readline("user=> ");
    if (line == NULL) return NULL;

    add_history(line);  // For up-arrow recall
    return make_string(line, strlen(line));
}

void repl_print(Value val) {
    // Print and flush immediately
    char* s = pr_str(val);
    fprintf(stdout, "%s\n", s);
    fflush(stdout);
    free(s);
}
```

### Pipes and Process I/O

```clojure
;; Run external process
(def proc (exec "ls" "-la"))
(def output (read-string (:stdout proc)))
(def exit-code (wait proc))

;; Pipe between processes
(def p1 (exec "grep" "pattern"))
(def p2 (exec "sort"))
(pipe (:stdout p1) (:stdin p2))

;; Shell command
(shell "ls -la | grep pattern")
```

### I/O Error Handling

```clojure
;; Try/catch for I/O errors
(try
  (def f (open "nonexistent.txt" :read))
  (catch IOException e
    (println "Error:" (.getMessage e))))

;; Check for EOF
(loop []
  (if-let [line (read-line f)]
    (do
      (process line)
      (recur))
    (println "Done")))
```

### Performance Considerations

1. **Buffering**: Default 8KB buffers reduce syscalls
2. **Batch writes**: Accumulate small writes
3. **Zero-copy**: Use `sendfile` for large transfers when possible
4. **Read-ahead**: Predict sequential reads
5. **Memory-mapped files**: For large file access

### Integration with Scheduler

I/O operations integrate seamlessly with the task scheduler:

```
Task calls read() → Buffer empty → Initiate async read → Block task
                                                           ↓
I/O Scheduler: epoll_wait() → Data ready → Wake task → Resume execution
```

### Future Extensions

1. **HTTP client/server** built on streams
2. **WebSocket** support
3. **TLS/SSL** streams
4. **Compression** streams (gzip, etc.)
5. **Async file system operations** (list dir, stat, etc.)
6. **Memory-mapped I/O**
7. **Direct I/O** (bypass cache)
8. **io_uring** support on Linux for even better performance

## Development Methodology

### Test-Driven Development

Beerlang follows a strict **test-first** approach where tests are written before implementation. This is particularly valuable for language and VM development.

#### Why TDD for a Programming Language?

1. **Tests define semantics** - They specify exact behavior of language constructs
2. **Early API design** - Writing tests first exposes interface problems
3. **Comprehensive edge cases** - Forces thinking about corner cases upfront
4. **Fearless refactoring** - Change internals without breaking behavior
5. **Living documentation** - Tests show how features should work
6. **Regression prevention** - Critical as VM and compiler evolve

#### Test Categories

**VM/Bytecode Tests:**
- Unit tests for each bytecode instruction
- Stack manipulation tests (push, pop, overflow, underflow)
- Memory management tests (refcounting, deferred deletion, cycles)
- Cooperative yielding and scheduling tests
- Exception handling and unwinding tests
- Instruction sequence integration tests
- Performance tests (cache residency, throughput)

**Compiler Tests:**
- Special form compilation (source → bytecode)
- Macro expansion correctness
- Optimization tests (tail call detection, constant folding)
- Closure capture and environment building
- Error reporting and source location tracking
- Edge cases (deeply nested forms, mutual recursion)

**Language/Runtime Tests:**
- Standard library function tests
- Data structure tests (persistent operations, structural sharing)
- REPL behavior and interaction tests
- Namespace and var resolution tests
- FFI and native library interaction
- Complete program integration tests

**Property-Based Tests:**
- Refcounting correctness (no leaks, no double-frees)
- Persistent data structure properties (immutability, efficiency)
- Arithmetic correctness (against reference implementations)
- Bytecode generation invariants

#### Test Organization

```
tests/
├── vm/
│   ├── instructions/     # Per-instruction unit tests
│   ├── stack/            # Stack operation tests
│   ├── memory/           # GC and refcounting tests
│   └── integration/      # Multi-instruction scenarios
├── compiler/
│   ├── special-forms/    # Compilation of each special form
│   ├── macros/           # Macro expansion tests
│   └── optimization/     # Optimization correctness
├── runtime/
│   ├── core/             # Standard library tests
│   ├── data-structures/  # List, vector, map tests
│   └── ffi/              # FFI integration tests
└── integration/
    ├── programs/         # Complete working programs
    └── benchmarks/       # Performance tests
```

#### TDD Workflow

1. **Write the test** - Define expected behavior
2. **Watch it fail** - Verify test catches the missing feature
3. **Implement minimally** - Just enough to pass the test
4. **Refactor** - Clean up while tests stay green
5. **Repeat** - Next feature

#### Benefits for Beerlang Specifically

- **Cache efficiency claims** - Benchmarks prove VM fits in cache
- **Cooperative yielding** - Tests verify tasks properly yield and resume
- **Refcounting** - Tests catch memory leaks and premature frees
- **Bytecode correctness** - Every instruction is validated
- **Cross-platform** - Same tests run on all target platforms

### Version Control Strategy

- Small, focused commits (one feature/test at a time)
- Tests committed before implementation
- Clear commit messages referencing what test they satisfy
- Feature branches for experimental changes

## Standard Library

The beerlang standard library provides essential functions and utilities for functional programming, data manipulation, I/O, concurrency, and more. It follows Clojure's core library philosophy but with beerlang-specific additions.

### Library Organization

```
beerlang.core          ; Core functions (auto-imported)
beerlang.string        ; String manipulation
beerlang.io            ; I/O operations
beerlang.async         ; Concurrency primitives
beerlang.math          ; Mathematical functions
beerlang.data          ; Data structure operations
beerlang.regex         ; Regular expressions
beerlang.time          ; Time and date
beerlang.test          ; Testing framework
beerlang.repl          ; REPL utilities
beerlang.bytecode      ; Bytecode metaprogramming
beerlang.system        ; System interface
```

### Core Library (beerlang.core)

The core namespace is automatically imported into every namespace. It provides fundamental operations.

#### Special Forms (Already Covered)

```clojure
def if do let quote fn loop try throw yield var . disasm asm
```

#### Core Functions - Sequence Operations

```clojure
;; Creation
(list 1 2 3)           ; Create list
(vector 1 2 3)         ; Create vector (also [1 2 3])
(hash-map :a 1 :b 2)   ; Create map (also {:a 1 :b 2})
(hash-set 1 2 3)       ; Create set (also #{1 2 3})
(range n)              ; Lazy sequence 0..n-1
(range start end)      ; Lazy sequence start..end-1
(repeat n x)           ; Repeat x n times
(repeatedly n f)       ; Call f n times

;; Access
(first coll)           ; First element
(rest coll)            ; Rest of collection (not first)
(next coll)            ; Next elements (or nil if empty)
(last coll)            ; Last element
(nth coll i)           ; Element at index i
(get map key)          ; Get value by key
(get map key default)  ; Get with default
(get-in m ks)          ; Nested get: (get-in {:a {:b 1}} [:a :b])

;; Modification (persistent/immutable)
(cons x coll)          ; Add to front
(conj coll x)          ; Add to collection (end for vectors)
(assoc map k v)        ; Associate key with value
(assoc-in m ks v)      ; Nested assoc
(dissoc map k)         ; Remove key
(update map k f)       ; Update value: (update {:a 1} :a inc)
(update-in m ks f)     ; Nested update

;; Transformation
(map f coll)           ; Map function over collection
(mapv f coll)          ; Map, return vector
(filter pred coll)     ; Keep elements matching predicate
(remove pred coll)     ; Remove elements matching predicate
(reduce f coll)        ; Reduce with function
(reduce f init coll)   ; Reduce with initial value
(into to from)         ; Pour from into to
(concat & colls)       ; Concatenate collections

;; Predicates
(seq coll)             ; Return seq or nil if empty
(empty? coll)          ; Is collection empty?
(not-empty coll)       ; Return coll or nil if empty
(some pred coll)       ; Return first truthy (pred x)
(every? pred coll)     ; True if all match predicate
(contains? coll k)     ; Does collection contain key?

;; Partitioning
(take n coll)          ; First n elements
(drop n coll)          ; All but first n
(take-while pred coll) ; Take while predicate true
(drop-while pred coll) ; Drop while predicate true
(partition n coll)     ; Partition into n-sized chunks
(partition-by f coll)  ; Partition when (f x) changes
(group-by f coll)      ; Group by result of f

;; Sorting
(sort coll)            ; Sort collection
(sort-by f coll)       ; Sort by key function
(reverse coll)         ; Reverse collection

;; Set operations
(union s1 s2)          ; Set union
(intersection s1 s2)   ; Set intersection
(difference s1 s2)     ; Set difference

;; Counting
(count coll)           ; Number of elements
(frequencies coll)     ; Map of value -> count
```

#### Core Functions - Predicates

```clojure
;; Type checking
(nil? x)               ; Is nil?
(some? x)              ; Not nil?
(true? x)              ; Is true?
(false? x)             ; Is false?
(number? x)            ; Is number?
(integer? x)           ; Is integer?
(float? x)             ; Is float?
(string? x)            ; Is string?
(keyword? x)           ; Is keyword?
(symbol? x)            ; Is symbol?
(list? x)              ; Is list?
(vector? x)            ; Is vector?
(map? x)               ; Is map?
(set? x)               ; Is set?
(fn? x)                ; Is function?
(coll? x)              ; Is collection?
(seq? x)               ; Is sequence?

;; Comparison
(= x y)                ; Equality
(not= x y)             ; Inequality
(< x y)                ; Less than
(<= x y)               ; Less or equal
(> x y)                ; Greater than
(>= x y)               ; Greater or equal
(compare x y)          ; Compare (-1, 0, 1)
(identical? x y)       ; Reference equality

;; Logic
(and x y ...)          ; Logical and (macro)
(or x y ...)           ; Logical or (macro)
(not x)                ; Logical not
```

#### Core Functions - Math

```clojure
;; Arithmetic
(+ x y ...)            ; Addition
(- x y ...)            ; Subtraction
(* x y ...)            ; Multiplication
(/ x y ...)            ; Division
(quot x y)             ; Quotient
(rem x y)              ; Remainder
(mod x y)              ; Modulo
(inc x)                ; Increment
(dec x)                ; Decrement

;; Comparison
(max x y ...)          ; Maximum
(min x y ...)          ; Minimum
(zero? x)              ; Is zero?
(pos? x)               ; Is positive?
(neg? x)               ; Is negative?
(even? x)              ; Is even?
(odd? x)               ; Is odd?

;; Bitwise
(bit-and x y)          ; Bitwise AND
(bit-or x y)           ; Bitwise OR
(bit-xor x y)          ; Bitwise XOR
(bit-not x)            ; Bitwise NOT
(bit-shift-left x n)   ; Left shift
(bit-shift-right x n)  ; Right shift
```

#### Core Functions - Functions

```clojure
;; Function composition
(comp & fs)            ; Compose functions: ((comp f g) x) = (f (g x))
(partial f & args)     ; Partial application
(complement f)         ; Logical complement
(constantly x)         ; Return function that returns x
(identity x)           ; Identity function

;; Application
(apply f args)         ; Apply function to arguments
(juxt & fs)            ; Juxtaposition: ((juxt f g) x) = [(f x) (g x)]

;; Memoization
(memoize f)            ; Memoize function results
```

#### Core Functions - Control Flow

```clojure
;; Conditionals (macros)
(when test & body)     ; Execute body if test true
(when-not test & body) ; Execute body if test false
(if-let [x expr] then else) ; Bind and test
(when-let [x expr] & body)  ; Bind and test
(cond & clauses)       ; Multi-way conditional
(condp pred expr & clauses) ; Conditional with predicate
(case expr & clauses)  ; Match expression

;; Iteration (macros)
(doseq [x coll] body)  ; Iterate for side effects
(dotimes [i n] body)   ; Iterate n times
(while test & body)    ; While loop
(for [x coll] expr)    ; List comprehension

;; Threading (macros)
(-> x & forms)         ; Thread-first
(->> x & forms)        ; Thread-last
(as-> x name & forms)  ; Thread with explicit name
(some-> x & forms)     ; Thread-first, stop on nil
```

#### Core Functions - I/O

```clojure
;; Output
(print & args)         ; Print args (no newline)
(println & args)       ; Print args with newline
(pr & args)            ; Print readable (no newline)
(prn & args)           ; Print readable with newline
(printf fmt & args)    ; Formatted print

;; String conversion
(str & args)           ; Convert to string
(pr-str x)             ; Readable string
(print-str x)          ; Display string
(prn-str x)            ; Readable string + newline

;; Reading
(read)                 ; Read from *in*
(read-line)            ; Read line from *in*
(read-string s)        ; Read from string
```

#### Core Functions - Namespace Operations

```clojure
;; Namespace management
(ns name & references) ; Declare namespace
(in-ns name)           ; Switch to namespace
(require & libs)       ; Load libraries
(use & libs)           ; Load and refer
(import & classes)     ; Import classes (for FFI)
(refer ns & filters)   ; Refer symbols from namespace

;; Namespace queries
(all-ns)               ; List all namespaces
(find-ns name)         ; Find namespace by name
(ns-name ns)           ; Get namespace name
(ns-publics ns)        ; Public vars in namespace
(ns-interns ns)        ; All vars in namespace
(ns-aliases ns)        ; Namespace aliases

;; Var operations
(def name value)       ; Define var
(defn name [args] body) ; Define function (macro)
(defmacro name [args] body) ; Define macro
(var-get v)            ; Get var value
(var-set v val)        ; Set var value (for dynamic vars)
(alter-var-root v f)   ; Alter var root
(binding [var val] body) ; Dynamic binding
```

#### Core Functions - Destructuring

Destructuring works in `let`, `fn`, `defn`, etc:

```clojure
;; Sequential destructuring
(let [[a b c] [1 2 3]]
  (+ a b c))           ; => 6

;; With rest
(let [[first & rest] [1 2 3 4]]
  [first rest])        ; => [1 (2 3 4)]

;; Map destructuring
(let [{:keys [a b]} {:a 1 :b 2}]
  (+ a b))             ; => 3

;; With defaults
(let [{:keys [a b] :or {b 10}} {:a 1}]
  (+ a b))             ; => 11

;; Nested
(let [{:keys [a] [b c] :seq} {:a 1 :seq [2 3]}]
  (+ a b c))           ; => 6
```

### String Library (beerlang.string)

```clojure
;; Case conversion
(upper-case s)         ; Convert to uppercase
(lower-case s)         ; Convert to lowercase
(capitalize s)         ; Capitalize first letter

;; Trimming
(trim s)               ; Trim whitespace
(trim-newline s)       ; Trim trailing newline
(triml s)              ; Trim left
(trimr s)              ; Trim right

;; Splitting/Joining
(split s re)           ; Split by regex
(split-lines s)        ; Split by newlines
(join sep coll)        ; Join with separator
(join coll)            ; Join without separator

;; Searching
(index-of s substr)    ; Find substring index
(last-index-of s substr) ; Find last occurrence
(includes? s substr)   ; Does s contain substr?
(starts-with? s prefix) ; Starts with prefix?
(ends-with? s suffix)  ; Ends with suffix?

;; Modification
(replace s match replacement) ; Replace
(replace-first s match replacement) ; Replace first
(reverse s)            ; Reverse string

;; Formatting
(format fmt & args)    ; Format string (printf-style)
```

### Async Library (beerlang.async)

Concurrency primitives for cooperative multitasking. Initial version focuses on essential, proven patterns.

```clojure
;; Task management
(spawn f)              ; Spawn new task
(spawn-link f)         ; Spawn linked task (failures propagate)
(await task)           ; Wait for task completion
(await task timeout)   ; Wait with timeout

;; Channels (CSP-style - core primitive)
(chan)                 ; Unbuffered channel
(chan n)               ; Buffered channel (size n)
(>! ch val)            ; Put (yields if full)
(<! ch)                ; Take (yields if empty)
(>!! ch val)           ; Blocking put (rarely needed)
(<!! ch)               ; Blocking take (rarely needed)
(close! ch)            ; Close channel
(alts! & channels)     ; Select from multiple channels
(timeout ms)           ; Timeout channel

;; Promises/Futures
(promise)              ; Create promise
(deliver p val)        ; Deliver promise value
(future & body)        ; Execute in task, return promise

;; Atoms (simple thread-safe shared state)
(atom x)               ; Create atom
(deref a)              ; Dereference (also @a)
(reset! a val)         ; Reset value
(swap! a f & args)     ; Atomic swap with function
(compare-and-set! a old new) ; CAS operation
```

**Note on Refs and Agents:**

These are **not included** in the initial version:

- **Refs (STM)**: Software Transactional Memory is complex to implement correctly:
  - Transaction log management
  - Conflict detection and retry logic
  - MVCC (Multi-Version Concurrency Control)
  - Deadlock prevention
  - Significant VM complexity

- **Agents**: Simpler than refs but still require:
  - Per-agent action queue
  - Dispatch infrastructure
  - Error propagation mechanism

**Alternative approaches:**
- **Atoms** handle most shared state needs (simple CAS)
- **Channels** handle task coordination and communication
- **Message passing** via channels is often cleaner than shared mutable state
- Can be added in future if proven necessary

**Example - Using atoms instead of refs:**

```clojure
;; Coordinated state with atoms (manual coordination)
(def account-a (atom 100))
(def account-b (atom 50))

(defn transfer [from to amount]
  ;; Manual coordination (not atomic across accounts)
  (swap! from - amount)
  (swap! to + amount))

;; Or use channels for coordination
(defn transfer-via-channel [from to amount]
  (let [ch (chan)]
    (spawn
      (>! from [:withdraw amount ch])
      (when (<! ch)
        (>! to [:deposit amount ch])))))
```

### Math Library (beerlang.math)

```clojure
;; Constants
Math/PI                ; π
Math/E                 ; e

;; Trigonometry
(sin x)                ; Sine
(cos x)                ; Cosine
(tan x)                ; Tangent
(asin x)               ; Arc sine
(acos x)               ; Arc cosine
(atan x)               ; Arc tangent
(atan2 y x)            ; Arc tangent (two-arg)

;; Exponential/Logarithmic
(exp x)                ; e^x
(log x)                ; Natural log
(log10 x)              ; Base-10 log
(pow x y)              ; x^y
(sqrt x)               ; Square root

;; Rounding
(floor x)              ; Floor
(ceil x)               ; Ceiling
(round x)              ; Round to nearest
(abs x)                ; Absolute value
(signum x)             ; Sign (-1, 0, 1)

;; Random
(rand)                 ; Random float 0-1
(rand-int n)           ; Random int 0..n-1
(rand-nth coll)        ; Random element
(shuffle coll)         ; Shuffle collection
```

### Regex Library (beerlang.regex)

Regular expressions via **FFI to native regex library** (PCRE or POSIX regex). Complex regex engines are non-trivial to implement from scratch, so we leverage existing battle-tested libraries.

```clojure
;; Pattern creation
(re-pattern s)         ; Compile pattern (FFI to PCRE/regex.h)
#"pattern"             ; Reader macro (compiles at read-time)

;; Matching
(re-matches re s)      ; Full match (returns match or nil)
(re-find re s)         ; Find first match
(re-seq re s)          ; Lazy sequence of all matches
(re-groups m)          ; Extract capture groups from match

;; Replacement
(re-replace re s replacement) ; Replace all occurrences
(re-replace-first re s replacement) ; Replace first occurrence
```

**Implementation approach:**

```c
// Pattern object (wraps PCRE regex)
struct Pattern {
    Object      header;
    String*     pattern_str;  // Original pattern
    pcre*       compiled;     // PCRE compiled regex
    int         capture_count; // Number of capture groups
};

// Compile pattern
Pattern* compile_pattern(const char* pattern_str) {
    const char* error;
    int erroffset;

    pcre* compiled = pcre_compile(
        pattern_str,
        0,                    // Options
        &error,
        &erroffset,
        NULL
    );

    if (compiled == NULL) {
        // Compilation error
        return NULL;
    }

    Pattern* p = allocate_pattern();
    p->pattern_str = make_string(pattern_str);
    p->compiled = compiled;

    // Get capture count
    pcre_fullinfo(compiled, NULL, PCRE_INFO_CAPTURECOUNT,
                  &p->capture_count);

    return p;
}

// Match
Value regex_find(Pattern* pat, String* str) {
    int ovector[30];  // Output vector for captures

    int rc = pcre_exec(
        pat->compiled,
        NULL,
        str->data,
        str->header.size,
        0,                // Start offset
        0,                // Options
        ovector,
        30
    );

    if (rc < 0) {
        return make_nil();  // No match
    }

    // Build match result with captures
    return make_match_result(str, ovector, rc);
}
```

**Reader macro implementation:**

```clojure
;; Reader expands #"pattern" at read-time
#"\\d+"  ; => (re-pattern "\\d+")

;; Patterns can be compiled once and reused
(def digit-pattern #"\\d+")
(re-find digit-pattern "abc123")  ; Efficient - pattern already compiled
```

**Alternative for simple cases:**

For very simple patterns, could implement basic matching without full regex:

```clojure
;; Simple string operations (no regex needed)
(string/includes? s "substring")
(string/starts-with? s "prefix")
(string/index-of s "pattern")
```

**Future considerations:**
- Could support multiple regex backends (PCRE, RE2, Rust regex)
- Could implement simple regex subset natively (for bootstrapping)
- Regex compilation could happen at compile-time for literal patterns

### Time Library (beerlang.time)

```clojure
;; Current time
(now)                  ; Current instant
(today)                ; Today's date

;; Construction
(instant ms)           ; From epoch milliseconds
(date year month day)  ; Create date
(time hour min sec)    ; Create time

;; Accessors
(year d)               ; Extract year
(month d)              ; Extract month
(day d)                ; Extract day
(hour t)               ; Extract hour
(minute t)             ; Extract minute
(second t)             ; Extract second

;; Arithmetic
(plus t duration)      ; Add duration
(minus t duration)     ; Subtract duration
(duration ms)          ; Duration from ms

;; Formatting
(format-time t fmt)    ; Format time
(parse-time s fmt)     ; Parse time
```

### Bytecode Library (beerlang.bytecode)

Bytecode metaprogramming utilities:

```clojure
;; Disassembly/Assembly (special forms)
(disasm f)             ; Disassemble function
(asm bytecode)         ; Assemble bytecode

;; Analysis
(bytecode-size f)      ; Count instructions
(instruction-histogram f) ; Frequency of instructions
(stack-depth-analysis bytecode) ; Max stack depth

;; Optimization
(optimize-bytecode bc) ; Apply optimizations
(inline-calls bc)      ; Inline small calls
(constant-fold bc)     ; Fold constants
(dead-code-elim bc)    ; Remove dead code

;; Pattern matching
(match-pattern bc pattern) ; Find instruction patterns
(replace-pattern bc from to) ; Replace patterns
```

### System Library (beerlang.system)

```clojure
;; System info
(os-name)              ; Operating system
(os-version)           ; OS version
(arch)                 ; Architecture (x86_64, arm64, etc.)
(cpu-count)            ; Number of CPUs

;; VM info
(vm-version)           ; Beerlang version
(vm-uptime)            ; VM uptime in ms
(memory-usage)         ; Memory statistics

;; Environment
(env var)              ; Get environment variable
(env)                  ; All environment variables
(property key)         ; System property

;; Process
(exit code)            ; Exit process
(shutdown-hook f)      ; Register shutdown hook
```

### Test Library (beerlang.test)

```clojure
;; Assertions
(is (= actual expected)) ; Assert equality
(is (thrown? Exception expr)) ; Assert exception

;; Test definition
(deftest test-name
  (is (= 1 1)))

;; Running tests
(run-tests)            ; Run all tests in namespace
(run-tests ns)         ; Run tests in namespace
(run-all-tests)        ; Run all tests

;; Fixtures
(use-fixtures :once fixture) ; Run once per namespace
(use-fixtures :each fixture) ; Run per test
```

### Implementation Notes

**Native vs Pure Beerlang:**

- **Native (C)**: Low-level operations (arithmetic, type checking, I/O)
- **Pure Beerlang**: Higher-level functions built on natives (map, filter, reduce)
- **Macros**: Control flow, destructuring (compile-time expansion)

**Lazy Sequences:**

Lazy sequences use **traditional thunks** (delayed computation), NOT `yield`. This keeps lazy evaluation separate from cooperative multitasking.

**Implementation approach:**

```clojure
;; lazy-seq macro wraps computation in a thunk
(defmacro lazy-seq [& body]
  `(LazySeq. (fn [] ~@body)))

;; Example: infinite range
(defn range
  ([] (range 0))
  ([start]
    (lazy-seq
      (cons start (range (inc start))))))

;; Usage
(take 5 (range))       ; Only computes first 5 elements
(take 5 (map inc (range))) ; Lazy map - composes lazily
```

**Under the hood:**

```c
// Lazy sequence object
struct LazySeq {
    Object   header;
    Value    thunk;      // Zero-arg function (or NULL if realized)
    Value    realized;   // Cached result (or NULL if not yet computed)
    bool     is_realized; // Have we computed this yet?
};

// Force lazy sequence
Value realize_lazy_seq(LazySeq* ls) {
    if (ls->is_realized) {
        return ls->realized;  // Return cached value
    }

    // Call thunk to compute value
    Value result = call_function(ls->thunk, 0, NULL);

    // Cache result
    ls->realized = result;
    ls->is_realized = true;

    // Release thunk (allow GC)
    ls->thunk = make_nil();

    return result;
}
```

**Why NOT use yield?**

1. **Different concerns:**
   - **Lazy evaluation**: Delay computation until needed (within a task)
   - **Cooperative yielding**: Give other tasks CPU time (between tasks)
   - Mixing them confuses two orthogonal concepts

2. **Lazy seqs are typically consumed quickly:**
   ```clojure
   (reduce + (take 1000 (range)))  ; Computed in one go
   ```

3. **If expensive, use tasks explicitly:**
   ```clojure
   ;; Compute lazily within a task
   (spawn
     (reduce + (take 1000000 (range))))

   ;; Parallel processing
   (let [tasks (map spawn
                    [(take 1M (range 0))
                     (take 1M (range 1M))])]
     (map await tasks))
   ```

4. **Simpler implementation:**
   - Just thunks + caching
   - No VM integration needed
   - Proven Clojure approach

**Automatic yielding consideration:**

We COULD insert automatic yields for long-running lazy sequences:

```clojure
;; Hypothetical: auto-yield every N iterations
(defn range-with-yield [start]
  (lazy-seq
    (when (= (rem start 1000) 0)
      (yield))  ; Every 1000th element
    (cons start (range-with-yield (inc start)))))
```

But this is **not recommended** because:
- Adds complexity
- Lazy seqs cross task boundaries (confusing)
- Better to explicitly spawn if computation is heavy

**Best practice:**

```clojure
;; Lazy within a task (normal)
(take 1000 (range))

;; Heavy computation? Use tasks explicitly
(let [result-task (spawn
                    (reduce + (range 1000000)))]
  (await result-task))

;; Process in chunks with tasks
(defn parallel-reduce [n chunk-size f init]
  (let [chunks (partition chunk-size (range n))
        tasks (map #(spawn (reduce f init %)) chunks)]
    (reduce f init (map await tasks))))
```

**Transducers:**
```clojure
;; Composable transformation pipelines
(def xf (comp (map inc) (filter even?)))
(transduce xf + 0 [1 2 3 4]) ; => 12
```

**Protocols (for future):**
```clojure
;; Protocol-based polymorphism
(defprotocol ISeq
  (first [s])
  (rest [s]))
```

## Open Questions and Future Work

### Near-term (v1.0)

1. **Bytecode format details**: Compact encoding, operand sizes, versioning
2. **Constant pool**: Structure, limits, deduplication strategy
3. **FFI marshalling**: Precise type conversion, struct handling
4. **Cycle detection**: Frequency and strategy for reference counting
5. **Regex backend**: Choose between PCRE, RE2, or POSIX regex

### Medium-term (v2.0)

6. **JIT compilation**: Strategy, tier selection, deoptimization
7. **Advanced tooling**:
   - Debugger with breakpoints and stepping
   - Profiler (sampling and instrumentation)
   - Memory profiler
   - Bytecode optimizer
8. **Performance optimizations**:
   - Inline caching for polymorphic calls
   - Type specialization
   - Escape analysis
   - Loop unrolling

### Long-term / Maybe

9. **Advanced concurrency**:
   - Refs/STM (if proven necessary - high complexity)
   - Agents (if async state updates needed beyond channels)
10. **Protocols**: Protocol-based polymorphism (Clojure-style)
11. **Records/Types**: User-defined types with optimized field access
12. **Multiple regex backends**: Pluggable regex engines
13. **Distributed computing**: Remote tasks, distributed channels
14. **Hot code reloading**: Update running system without restart
15. **Native compilation**: AOT compiler to native code

## Implementation Priorities

### Implementation Language: C

**C is the recommended choice** for beerlang's VM implementation:

**Advantages:**
- Direct memory control (essential for cache-efficient design)
- Zero runtime overhead
- Portable across all platforms
- Simple FFI to libraries (GMP, PCRE, system calls)
- Predictable performance characteristics
- Well-proven for VM implementation (Lua, Python, Ruby all use C)

**Project Structure:**
```
beerlang/
├── src/
│   ├── vm/           # Virtual machine core
│   ├── types/        # Object representations
│   ├── memory/       # Reference counting, allocation
│   ├── reader/       # S-expression reader
│   ├── compiler/     # Bytecode compiler
│   ├── runtime/      # Runtime library
│   ├── scheduler/    # Task scheduler
│   ├── io/           # I/O system
│   └── repl/         # REPL
├── include/          # Public headers
├── tests/            # Test suite
│   ├── vm/
│   ├── compiler/
│   └── runtime/
├── examples/         # Example programs
└── docs/            # Documentation
```

### Phased Implementation Approach

Each phase builds on the previous, allowing incremental testing and validation.

---

### Phase 1: Foundation (Week 1-2)

**Goal:** Basic VM that can execute hand-written bytecode

**Tasks:**
1. **Value representation**
   - Tagged pointers (64-bit)
   - Immediate values (fixnum, char, nil, true, false)
   - Heap object header structure

2. **Memory management basics**
   - Simple allocator
   - Reference counting (inc/dec)
   - No cycle detection yet

3. **Basic types**
   - Fixnum operations
   - Boolean operations
   - Nil

4. **VM core**
   - Value stack
   - Instruction fetch/decode/execute loop
   - Basic instructions: PUSH_INT, ADD, SUB, POP, DUP

5. **Testing**
   - Hand-write bytecode
   - Test arithmetic
   - Test stack operations

**Milestone:** Execute `2 + 3 * 4` from hand-written bytecode

---

### Phase 2: More Types & Instructions (Week 3)

**Goal:** Complete basic type system

**Tasks:**
1. **String type**
   - Heap-allocated strings
   - String equality
   - String printing

2. **Symbol type**
   - Symbol interning
   - Global symbol table

3. **Collections (simple versions)**
   - Cons cells (linked lists)
   - Basic vector (dynamic array)
   - Basic hash map (simple hash table)

4. **More VM instructions**
   - Comparison (LT, GT, EQ, etc.)
   - Logic (NOT, etc.)
   - Stack manipulation (SWAP, OVER)
   - Jump instructions (JUMP, JUMP_IF_FALSE)

5. **Testing**
   - Test each type
   - Test collection operations
   - Test control flow

**Milestone:** Execute conditional logic from hand-written bytecode

---

### Phase 3: Functions & Calls (Week 4)

**Goal:** Function calls and basic closures

**Tasks:**
1. **Function objects**
   - Function type
   - Code pointer
   - Arity

2. **Calling convention**
   - CALL instruction
   - RETURN instruction
   - ENTER instruction
   - Stack frames
   - Local variables (LOAD_LOCAL, STORE_LOCAL)

3. **Simple closures**
   - Capture variables
   - LOAD_CLOSURE instruction
   - MAKE_CLOSURE instruction

4. **Testing**
   - Test function calls
   - Test recursion
   - Test closures

**Milestone:** Execute recursive factorial from hand-written bytecode

---

### Phase 4: Reader (Week 5)

**Goal:** Parse Clojure syntax into data structures

**Tasks:**
1. **Lexer**
   - Tokenize input
   - Handle whitespace and comments

2. **Parser**
   - Parse lists, vectors, maps
   - Parse literals (numbers, strings, symbols, keywords)
   - Parse reader macros (quote, etc.)

3. **Error handling**
   - Line/column tracking
   - Meaningful error messages

4. **Testing**
   - Test parsing of all syntax forms
   - Test error cases

**Milestone:** Parse `(+ 1 2)` into data structures

---

### Phase 5: Simple Compiler (Week 6-7)

**Goal:** Compile S-expressions to bytecode

**Tasks:**
1. **Compilation pipeline**
   - Analyze forms
   - Generate bytecode
   - Constant pool

2. **Compile literals**
   - Numbers, strings, keywords, etc.

3. **Compile function calls**
   - Argument evaluation
   - Function evaluation
   - CALL instruction

4. **Compile special forms (basic)**
   - `if`
   - `do`
   - `def`
   - `fn`

5. **Testing**
   - Test each compilation path
   - Compare generated bytecode

**Milestone:** Compile and execute `(def x (+ 1 2))` `(println x)`

---

### Phase 6: REPL v1 (Week 8)

**Goal:** Working read-eval-print loop

**Tasks:**
1. **REPL loop**
   - Read from stdin
   - Compile
   - Execute
   - Print result

2. **Namespace basics**
   - Current namespace
   - Var storage
   - Var lookup

3. **Core functions (native)**
   - Arithmetic: +, -, *, /
   - Comparison: =, <, >
   - I/O: println
   - Collections: list, vector, first, rest

4. **Testing**
   - Interactive testing
   - REPL sessions

**Milestone:** Interactive REPL that can define functions and execute them

---

### Phase 7: Complete Compiler (Week 9-10)

**Goal:** All special forms and optimizations

**Tasks:**
1. **Remaining special forms**
   - `let`
   - `loop`/`recur`
   - `try`/`catch`/`finally`
   - `throw`
   - `quote`
   - `var`

2. **Lexical environments**
   - Proper scoping
   - Closure capture detection

3. **Tail call optimization**
   - Detect tail position
   - Emit TAIL_CALL
   - Test recursion

4. **Macro system**
   - `defmacro`
   - Macro expansion
   - Compile-time evaluation

5. **Testing**
   - Comprehensive compiler tests
   - Test all special forms

**Milestone:** Compile complex programs with macros and recursion

---

### Phase 8: Concurrency (Week 11-12)

**Goal:** Cooperative multitasking

**Tasks:**
1. **Task structure**
   - Task object
   - Task states
   - Task stack

2. **Scheduler**
   - Ready queues
   - Context switching
   - Worker threads

3. **spawn/await**
   - Create tasks
   - Wait for completion

4. **Channels**
   - Channel object
   - Send/receive
   - Blocking/yielding

5. **Testing**
   - Test task creation
   - Test channel communication
   - Test concurrent programs

**Milestone:** Multiple tasks communicating via channels

---

### Phase 9: I/O System (Week 13-14)

**Goal:** Non-blocking I/O with reactor pattern

**Tasks:**
1. **Stream abstraction**
   - Stream object
   - Buffering

2. **Reactor threads**
   - epoll/kqueue integration
   - Event loop
   - Wake blocked tasks

3. **File I/O**
   - Open, read, write, close
   - Async operations

4. **Standard streams**
   - stdin, stdout, stderr
   - REPL integration

5. **Testing**
   - Test file operations
   - Test concurrent I/O

**Milestone:** Read/write files concurrently from multiple tasks

---

### Phase 10: Standard Library (Week 15-16)

**Goal:** Essential library functions

**Tasks:**
1. **Core functions (pure beerlang)**
   - Sequence operations (map, filter, reduce)
   - Collection operations
   - Higher-order functions

2. **String library**
   - String manipulation
   - Formatting

3. **Math library**
   - Trigonometry
   - Exponential/log
   - Random

4. **Regex (FFI to PCRE)**
   - Pattern compilation
   - Matching
   - Replacement

5. **Testing**
   - Test each library function
   - Integration tests

**Milestone:** Rich standard library for practical programming

---

### Phase 11: Polish & Optimization (Week 17-18)

**Goal:** Production-ready v1.0

**Tasks:**
1. **Memory optimizations**
   - Deferred deletion
   - Cycle detection (simple)

2. **Compiler optimizations**
   - Constant folding
   - Dead code elimination

3. **Error handling**
   - Better error messages
   - Stack traces

4. **Documentation**
   - API documentation
   - Tutorial
   - Examples

5. **Benchmarking**
   - Performance tests
   - Memory profiling
   - Optimization based on results

**Milestone:** Beerlang v1.0 release!

---

### Development Tools & Practices

**Build System:**
```bash
# Makefile targets
make          # Build VM
make test     # Run tests
make repl     # Start REPL
make clean    # Clean build
```

**Testing Strategy:**
- Unit tests for each module (using any C test framework)
- Integration tests for end-to-end functionality
- Hand-written bytecode tests early on
- Beerlang test files once REPL works

**Version Control:**
- Git from day one
- Small, focused commits
- Test before commit
- Document design decisions in commit messages

**Dependencies:**
- GMP (arbitrary precision arithmetic) - Bruno Levy's port
- PCRE or POSIX regex (for regex support)
- Platform-specific: epoll (Linux), kqueue (BSD/macOS)

**Debugging Tools:**
- gdb/lldb for C debugging
- Bytecode disassembler (implement early)
- Trace mode (print executed instructions)
- Memory leak detection (valgrind)

---

### Quick Win Strategy

For **fastest path to a working demo:**

1. **Week 1**: VM core + basic arithmetic
2. **Week 2**: More types + control flow
3. **Week 3**: Function calls
4. **Week 4**: Reader
5. **Week 5**: Simple compiler
6. **Week 6**: REPL

After 6 weeks, you have a **working REPL** that can:
- Define functions
- Execute arithmetic
- Use basic control flow
- Print results

Then iterate to add remaining features!

## Implementation Status

**Planning Phase Complete**: Language specification and architecture fully designed.

**Next Step**: Begin Phase 1 - Foundation (VM core and basic types)

## References

- GMP arbitrary precision arithmetic: Bruno Levy's port (GitHub)
- Clojure: https://clojure.org/
- Stack-based VM architectures
