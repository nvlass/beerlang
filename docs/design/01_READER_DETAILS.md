## Reader Details

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

