### Printer details

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
