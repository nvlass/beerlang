/* Reader Implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include "reader.h"
#include "buffer.h"
#include "beerlang.h"

/* ================================================================
 * Character Utilities
 * ================================================================ */

/* Peek at current character without advancing */
static char peek(Reader* r) {
    return buffer_at(r->buffer, r->pos);
}

/* Peek at next character (lookahead) */
static char peek_next(Reader* r) {
    return buffer_at(r->buffer, r->pos + 1);
}

/* Advance position and return current character */
static char advance(Reader* r) {
    char ch = buffer_at(r->buffer, r->pos);

    if (ch == '\0') {
        return '\0';
    }

    r->pos++;

    if (ch == '\n') {
        r->line++;
        r->column = 0;
    } else {
        r->column++;
    }

    return ch;
}

/* Consume expected character, error if mismatch */
static bool consume(Reader* r, char expected) {
    if (peek(r) != expected) {
        return false;
    }
    advance(r);
    return true;
}

/* Check if character is whitespace (including comma!) */
static bool is_whitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' ||
           ch == '\r' || ch == ',';
}

/* Check if character can start a symbol */
static bool is_symbol_start(char ch) {
    return isalpha(ch) || strchr("*+!-_?$%&=<>", ch) != NULL;
}

/* Check if character can appear in a symbol */
static bool is_symbol_char(char ch) {
    if (ch == '\0') return false;  /* Don't read past end of input */
    return isalnum(ch) || strchr("*+!-_?$%&=<>./:", ch) != NULL;
}

/* ================================================================
 * Whitespace and Comments
 * ================================================================ */

static void skip_whitespace_and_comments(Reader* r) {
    while (true) {
        char ch = peek(r);

        if (is_whitespace(ch)) {
            advance(r);
        } else if (ch == ';') {
            /* Line comment - skip until newline */
            while (peek(r) != '\n' && peek(r) != '\0') {
                advance(r);
            }
        } else {
            break;
        }
    }
}

/* ================================================================
 * Error Handling
 * ================================================================ */

static void reader_error(Reader* r, const char* fmt, ...) {
    r->error = true;

    va_list args;
    va_start(args, fmt);

    int prefix_len = snprintf(r->error_msg, sizeof(r->error_msg),
                              "%s:%zu:%zu: ",
                              r->filename ? r->filename : "<stdin>",
                              r->line, r->column);

    vsnprintf(r->error_msg + prefix_len,
              sizeof(r->error_msg) - prefix_len,
              fmt, args);

    va_end(args);
}

/* ================================================================
 * Forward Declarations
 * ================================================================ */

static Value read_form(Reader* r);

/* ================================================================
 * Number Reading
 * ================================================================ */

static Value read_number(Reader* r) {
    char buf[256];
    int i = 0;
    bool negative = false;

    /* Handle sign */
    if (peek(r) == '-') {
        negative = true;
        buf[i++] = advance(r);
    } else if (peek(r) == '+') {
        buf[i++] = advance(r);
    }

    /* Check for hex prefix */
    bool is_hex = false;
    if (peek(r) == '0' && (peek_next(r) == 'x' || peek_next(r) == 'X')) {
        buf[i++] = advance(r);  /* '0' */
        buf[i++] = advance(r);  /* 'x' */
        is_hex = true;
    }

    /* Read digits */
    while (isdigit(peek(r)) || (is_hex && isxdigit(peek(r)))) {
        if (i >= (int)sizeof(buf) - 1) {
            reader_error(r, "Number too long");
            return VALUE_NIL;
        }
        buf[i++] = advance(r);
    }

    /* Check for float: decimal point or exponent */
    bool is_float_lit = false;
    if (!is_hex && peek(r) == '.' && isdigit(peek_next(r))) {
        is_float_lit = true;
        buf[i++] = advance(r);  /* '.' */
        while (isdigit(peek(r))) {
            if (i >= (int)sizeof(buf) - 1) { reader_error(r, "Number too long"); return VALUE_NIL; }
            buf[i++] = advance(r);
        }
    }
    if (!is_hex && (peek(r) == 'e' || peek(r) == 'E')) {
        is_float_lit = true;
        buf[i++] = advance(r);  /* 'e' or 'E' */
        if (peek(r) == '+' || peek(r) == '-') {
            if (i >= (int)sizeof(buf) - 1) { reader_error(r, "Number too long"); return VALUE_NIL; }
            buf[i++] = advance(r);
        }
        while (isdigit(peek(r))) {
            if (i >= (int)sizeof(buf) - 1) { reader_error(r, "Number too long"); return VALUE_NIL; }
            buf[i++] = advance(r);
        }
    }

    buf[i] = '\0';

    if (is_float_lit) {
        return make_float(strtod(buf, NULL));
    }

    /* Parse the number */
    errno = 0;
    long long val;

    if (is_hex) {
        val = strtoll(buf, NULL, 16);
    } else {
        val = strtoll(buf, NULL, 10);
    }

    /* Check for overflow - fixnums are 61-bit signed */
    /* Range: -2^60 to 2^60-1 */
    if (errno == ERANGE || val < -(1LL << 60) || val > ((1LL << 60) - 1)) {
        /* Use bigint */
        return bigint_from_string(buf, is_hex ? 16 : 10);
    }

    return make_fixnum(val);
}

/* ================================================================
 * String Reading
 * ================================================================ */

static Value read_string_literal(Reader* r) {
    if (!consume(r, '"')) {
        reader_error(r, "Expected '\"'");
        return VALUE_NIL;
    }

    char buf[4096];
    int i = 0;

    while (peek(r) != '"' && peek(r) != '\0') {
        if (i >= (int)sizeof(buf) - 1) {
            reader_error(r, "String too long");
            return VALUE_NIL;
        }

        if (peek(r) == '\\') {
            /* Escape sequence */
            advance(r);
            char escaped = advance(r);

            switch (escaped) {
                case 'n':  buf[i++] = '\n'; break;
                case 't':  buf[i++] = '\t'; break;
                case 'r':  buf[i++] = '\r'; break;
                case '\\': buf[i++] = '\\'; break;
                case '"':  buf[i++] = '"'; break;
                case 'b':  buf[i++] = '\b'; break;
                case 'f':  buf[i++] = '\f'; break;
                case '\0':
                    reader_error(r, "Unterminated escape sequence");
                    return VALUE_NIL;
                default:
                    reader_error(r, "Unknown escape sequence: \\%c", escaped);
                    return VALUE_NIL;
            }
        } else {
            buf[i++] = advance(r);
        }
    }

    if (peek(r) != '"') {
        reader_error(r, "Unterminated string");
        return VALUE_NIL;
    }
    consume(r, '"');

    buf[i] = '\0';
    return string_from_buffer(buf, i);
}

/* ================================================================
 * Character Reading
 * ================================================================ */

static Value read_character(Reader* r) {
    if (!consume(r, '\\')) {
        reader_error(r, "Expected '\\'");
        return VALUE_NIL;
    }

    char buf[32];
    int i = 0;

    /* Read character name or single char */
    while (isalpha(peek(r))) {
        if (i >= (int)sizeof(buf) - 1) {
            reader_error(r, "Character name too long");
            return VALUE_NIL;
        }
        buf[i++] = advance(r);
    }

    if (i == 0) {
        /* Single character */
        char ch = advance(r);
        if (ch == '\0') {
            reader_error(r, "Unexpected EOF in character literal");
            return VALUE_NIL;
        }
        return make_char(ch);
    }

    buf[i] = '\0';

    /* Named characters */
    if (strcmp(buf, "newline") == 0) return make_char('\n');
    if (strcmp(buf, "space") == 0) return make_char(' ');
    if (strcmp(buf, "tab") == 0) return make_char('\t');
    if (strcmp(buf, "return") == 0) return make_char('\r');
    if (strcmp(buf, "backspace") == 0) return make_char('\b');
    if (strcmp(buf, "formfeed") == 0) return make_char('\f');

    /* Single named character (e.g., \a) */
    if (i == 1) {
        return make_char(buf[0]);
    }

    reader_error(r, "Unknown character: \\%s", buf);
    return VALUE_NIL;
}

/* ================================================================
 * Symbol and Keyword Reading
 * ================================================================ */

static Value read_symbol_or_special(Reader* r) {
    char buf[256];
    int i = 0;

    /* Read symbol characters */
    while (is_symbol_char(peek(r))) {
        if (i >= (int)sizeof(buf) - 1) {
            reader_error(r, "Symbol too long");
            return VALUE_NIL;
        }
        buf[i++] = advance(r);
    }
    buf[i] = '\0';

    /* Check for special values */
    if (strcmp(buf, "nil") == 0) return VALUE_NIL;
    if (strcmp(buf, "true") == 0) return VALUE_TRUE;
    if (strcmp(buf, "false") == 0) return VALUE_FALSE;

    /* Parse namespace/name */
    char* slash = strchr(buf, '/');
    if (slash != NULL && slash != buf && slash[1] != '\0') {
        /* Qualified symbol: namespace/name (but not bare "/" or "foo/") */
        *slash = '\0';
        return symbol_intern_ns(buf, slash + 1);
    } else {
        /* Unqualified symbol (including bare "/") */
        return symbol_intern(buf);
    }
}

static Value read_keyword(Reader* r) {
    if (!consume(r, ':')) {
        reader_error(r, "Expected ':'");
        return VALUE_NIL;
    }

    char buf[256];
    int i = 0;

    /* Read keyword characters */
    while (is_symbol_char(peek(r))) {
        if (i >= (int)sizeof(buf) - 1) {
            reader_error(r, "Keyword too long");
            return VALUE_NIL;
        }
        buf[i++] = advance(r);
    }
    buf[i] = '\0';

    if (i == 0) {
        reader_error(r, "Keyword cannot be empty");
        return VALUE_NIL;
    }

    /* Parse namespace/name */
    char* slash = strchr(buf, '/');
    if (slash != NULL) {
        /* Qualified keyword: :namespace/name */
        *slash = '\0';
        return keyword_intern_ns(buf, slash + 1);
    } else {
        /* Unqualified keyword */
        return keyword_intern(buf);
    }
}

/* ================================================================
 * Collection Reading
 * ================================================================ */

static Value read_list(Reader* r) {
    if (!consume(r, '(')) {
        reader_error(r, "Expected '('");
        return VALUE_NIL;
    }

    skip_whitespace_and_comments(r);

    /* Use a vector to accumulate elements */
    Value vec = vector_create(16);

    while (peek(r) != ')' && peek(r) != '\0') {
        Value elem = read_form(r);
        if (r->error) {
            object_release(vec);
            return VALUE_NIL;
        }

        vector_push(vec, elem);
        skip_whitespace_and_comments(r);
    }

    if (peek(r) != ')') {
        reader_error(r, "Expected ')' but got EOF");
        object_release(vec);
        return VALUE_NIL;
    }
    consume(r, ')');

    /* Convert vector to list */
    Value list = vector_to_list(vec);
    object_release(vec);
    return list;
}

static Value read_vector(Reader* r) {
    if (!consume(r, '[')) {
        reader_error(r, "Expected '['");
        return VALUE_NIL;
    }

    skip_whitespace_and_comments(r);

    Value vec = vector_create(16);

    while (peek(r) != ']' && peek(r) != '\0') {
        Value elem = read_form(r);
        if (r->error) {
            object_release(vec);
            return VALUE_NIL;
        }

        vector_push(vec, elem);
        skip_whitespace_and_comments(r);
    }

    if (peek(r) != ']') {
        reader_error(r, "Expected ']' but got EOF");
        object_release(vec);
        return VALUE_NIL;
    }
    consume(r, ']');

    return vec;
}

static Value read_map(Reader* r) {
    if (!consume(r, '{')) {
        reader_error(r, "Expected '{'");
        return VALUE_NIL;
    }

    skip_whitespace_and_comments(r);

    /* Use vector to collect key-value pairs */
    Value vec = vector_create(16);

    while (peek(r) != '}' && peek(r) != '\0') {
        /* Read key */
        Value key = read_form(r);
        if (r->error) {
            object_release(vec);
            return VALUE_NIL;
        }

        skip_whitespace_and_comments(r);

        /* Check for value */
        if (peek(r) == '}' || peek(r) == '\0') {
            reader_error(r, "Map literal must contain even number of forms");
            object_release(vec);
            return VALUE_NIL;
        }

        /* Read value */
        Value val = read_form(r);
        if (r->error) {
            object_release(vec);
            return VALUE_NIL;
        }

        vector_push(vec, key);
        vector_push(vec, val);
        skip_whitespace_and_comments(r);
    }

    if (peek(r) != '}') {
        reader_error(r, "Expected '}' but got EOF");
        object_release(vec);
        return VALUE_NIL;
    }
    consume(r, '}');

    /* Convert vector to hashmap */
    Value map = hashmap_from_vec(vec);
    object_release(vec);
    return map;
}

/* ================================================================
 * Reader Macros
 * ================================================================ */

static Value read_quote(Reader* r) {
    if (!consume(r, '\'')) {
        reader_error(r, "Expected \"'\"");
        return VALUE_NIL;
    }

    Value form = read_form(r);
    if (r->error) {
        return VALUE_NIL;
    }

    /* Build (quote form) */
    Value quote_sym = symbol_intern("quote");
    Value list = cons(quote_sym, cons(form, VALUE_NIL));
    return list;
}

/* Read quasiquote: `form -> (quasiquote form) */
static Value read_quasiquote(Reader* r) {
    if (!consume(r, '`')) {
        reader_error(r, "Expected \"`\"");
        return VALUE_NIL;
    }

    Value form = read_form(r);
    if (r->error) {
        return VALUE_NIL;
    }

    Value sym = symbol_intern("quasiquote");
    Value list = cons(sym, cons(form, VALUE_NIL));
    return list;
}

/* Read unquote: ~form -> (unquote form), ~@form -> (unquote-splicing form) */
static Value read_unquote(Reader* r) {
    if (!consume(r, '~')) {
        reader_error(r, "Expected \"~\"");
        return VALUE_NIL;
    }

    const char* sym_name;
    if (peek(r) == '@') {
        advance(r);
        sym_name = "unquote-splicing";
    } else {
        sym_name = "unquote";
    }

    Value form = read_form(r);
    if (r->error) {
        return VALUE_NIL;
    }

    Value sym = symbol_intern(sym_name);
    Value list = cons(sym, cons(form, VALUE_NIL));
    return list;
}

/* ================================================================
 * Main Reading Logic
 * ================================================================ */

static Value read_form(Reader* r) {
    skip_whitespace_and_comments(r);

    char ch = peek(r);

    switch (ch) {
        case '\0':
            return VALUE_NIL;

        case '(':
            return read_list(r);

        case '[':
            return read_vector(r);

        case '{':
            return read_map(r);

        case ')':
        case ']':
        case '}':
            reader_error(r, "Unexpected closing delimiter '%c'", ch);
            return VALUE_NIL;

        case '\'':
            return read_quote(r);

        case '`':
            return read_quasiquote(r);

        case '~':
            return read_unquote(r);

        case '"':
            return read_string_literal(r);

        case '\\':
            return read_character(r);

        case ':':
            return read_keyword(r);

        case '@': {
            advance(r);
            Value form = read_form(r);
            if (r->error) return VALUE_NIL;
            Value deref_sym = symbol_intern("deref");
            return cons(deref_sym, cons(form, VALUE_NIL));
        }

        default:
            if (ch == '/' && !is_symbol_char(peek_next(r))) {
                /* Bare "/" is the division symbol */
                advance(r);
                return symbol_intern("/");
            } else if (isdigit(ch) || (ch == '-' && isdigit(peek_next(r)))) {
                return read_number(r);
            } else if (is_symbol_start(ch)) {
                return read_symbol_or_special(r);
            } else {
                reader_error(r, "Unexpected character: '%c'", ch);
                return VALUE_NIL;
            }
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

Reader* reader_new(const char* source, const char* filename) {
    Reader* r = (Reader*)malloc(sizeof(Reader));
    if (!r) {
        return NULL;
    }

    r->buffer = buffer_new_string(source);
    if (!r->buffer) {
        free(r);
        return NULL;
    }

    r->pos = 0;
    r->line = 1;
    r->column = 0;
    r->filename = filename;
    r->error = false;
    r->error_msg[0] = '\0';

    return r;
}

Reader* reader_new_file(FILE* file, const char* filename) {
    if (!file) {
        return NULL;
    }

    Reader* r = (Reader*)malloc(sizeof(Reader));
    if (!r) {
        fclose(file);  /* Close file if malloc fails */
        return NULL;
    }

    r->buffer = buffer_new_file(file);
    if (!r->buffer) {
        free(r);
        return NULL;
    }

    r->pos = 0;
    r->line = 1;
    r->column = 0;
    r->filename = filename;
    r->error = false;
    r->error_msg[0] = '\0';

    return r;
}

void reader_free(Reader* r) {
    if (r) {
        buffer_free(r->buffer);
        free(r);
    }
}

Value reader_read(Reader* r) {
    if (r->error) {
        return VALUE_NIL;
    }

    skip_whitespace_and_comments(r);

    if (peek(r) == '\0') {
        /* EOF without error */
        return VALUE_NIL;
    }

    return read_form(r);
}

Value reader_read_all(Reader* r) {
    Value vec = vector_create(16);

    while (peek(r) != '\0' && !r->error) {
        skip_whitespace_and_comments(r);

        if (peek(r) == '\0') {
            break;
        }

        Value form = read_form(r);
        if (r->error) {
            object_release(vec);
            return VALUE_NIL;
        }

        vector_push(vec, form);
    }

    return vec;
}

bool reader_has_error(Reader* r) {
    return r->error;
}

const char* reader_error_msg(Reader* r) {
    return r->error_msg;
}

Value read_string(const char* source) {
    Reader* r = reader_new(source, "<string>");
    Value result = reader_read(r);

    if (r->error) {
        fprintf(stderr, "Reader error: %s\n", r->error_msg);
        reader_free(r);
        return VALUE_NIL;
    }

    reader_free(r);
    return result;
}
