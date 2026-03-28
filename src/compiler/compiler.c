/* Compiler Implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include "compiler.h"
#include "beerlang.h"
#include "namespace.h"
#include "function.h"
#include "vm.h"

/* Initial buffer capacity */
#define INITIAL_BYTECODE_CAPACITY 256

/* =================================================================
 * Bytecode Buffer Implementation
 * ================================================================= */

BytecodeBuffer* bytecode_buffer_new(void) {
    BytecodeBuffer* buf = (BytecodeBuffer*)malloc(sizeof(BytecodeBuffer));
    if (!buf) return NULL;

    buf->code = (uint8_t*)malloc(INITIAL_BYTECODE_CAPACITY);
    if (!buf->code) {
        free(buf);
        return NULL;
    }

    buf->length = 0;
    buf->capacity = INITIAL_BYTECODE_CAPACITY;
    return buf;
}

void bytecode_buffer_free(BytecodeBuffer* buf) {
    if (buf) {
        free(buf->code);
        free(buf);
    }
}

static void bytecode_ensure_capacity(BytecodeBuffer* buf, size_t needed) {
    if (buf->length + needed <= buf->capacity) {
        return;
    }

    size_t new_capacity = buf->capacity;
    while (new_capacity < buf->length + needed) {
        new_capacity *= 2;
    }

    uint8_t* new_code = (uint8_t*)realloc(buf->code, new_capacity);
    if (!new_code) {
        fprintf(stderr, "Fatal: Out of memory in bytecode buffer\n");
        abort();
    }

    buf->code = new_code;
    buf->capacity = new_capacity;
}

void bytecode_emit_byte(BytecodeBuffer* buf, uint8_t byte) {
    bytecode_ensure_capacity(buf, 1);
    buf->code[buf->length++] = byte;
}

void bytecode_emit_uint16(BytecodeBuffer* buf, uint16_t value) {
    bytecode_ensure_capacity(buf, 2);
    buf->code[buf->length++] = (uint8_t)(value & 0xFF);
    buf->code[buf->length++] = (uint8_t)((value >> 8) & 0xFF);
}

void bytecode_emit_uint32(BytecodeBuffer* buf, uint32_t value) {
    bytecode_ensure_capacity(buf, 4);
    buf->code[buf->length++] = (uint8_t)(value & 0xFF);
    buf->code[buf->length++] = (uint8_t)((value >> 8) & 0xFF);
    buf->code[buf->length++] = (uint8_t)((value >> 16) & 0xFF);
    buf->code[buf->length++] = (uint8_t)((value >> 24) & 0xFF);
}

void bytecode_emit_int64(BytecodeBuffer* buf, int64_t value) {
    bytecode_ensure_capacity(buf, 8);
    uint64_t uvalue = (uint64_t)value;
    for (int i = 0; i < 8; i++) {
        buf->code[buf->length++] = (uint8_t)((uvalue >> (i * 8)) & 0xFF);
    }
}

size_t bytecode_current_offset(BytecodeBuffer* buf) {
    return buf->length;
}

void bytecode_patch_uint16(BytecodeBuffer* buf, size_t offset, uint16_t value) {
    assert(offset + 1 < buf->length);
    buf->code[offset] = (uint8_t)(value & 0xFF);
    buf->code[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

void bytecode_patch_uint32(BytecodeBuffer* buf, size_t offset, uint32_t value) {
    assert(offset + 3 < buf->length);
    buf->code[offset] = (uint8_t)(value & 0xFF);
    buf->code[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    buf->code[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    buf->code[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

void bytecode_patch_int32(BytecodeBuffer* buf, size_t offset, int32_t value) {
    assert(offset + 3 < buf->length);
    uint32_t uvalue = (uint32_t)value;
    buf->code[offset] = (uint8_t)(uvalue & 0xFF);
    buf->code[offset + 1] = (uint8_t)((uvalue >> 8) & 0xFF);
    buf->code[offset + 2] = (uint8_t)((uvalue >> 16) & 0xFF);
    buf->code[offset + 3] = (uint8_t)((uvalue >> 24) & 0xFF);
}

/* =================================================================
 * Constant Pool Implementation
 * ================================================================= */

ConstantPool* constant_pool_new(void) {
    ConstantPool* pool = (ConstantPool*)malloc(sizeof(ConstantPool));
    if (!pool) return NULL;

    pool->constants_vec = vector_create(16);
    pool->const_map = hashmap_create_default();

    return pool;
}

void constant_pool_free(ConstantPool* pool) {
    if (pool) {
        object_release(pool->constants_vec);
        object_release(pool->const_map);
        free(pool);
    }
}

int constant_pool_add(ConstantPool* pool, Value constant) {
    /* Check if constant already exists (deduplication) */
    Value idx_val = hashmap_get(pool->const_map, constant);
    if (!is_nil(idx_val)) {
        return (int)untag_fixnum(idx_val);
    }

    /* Add new constant */
    int index = (int)vector_length(pool->constants_vec);
    vector_push(pool->constants_vec, constant);
    hashmap_set(pool->const_map, constant, make_fixnum(index));

    return index;
}

Value constant_pool_get(ConstantPool* pool, int index) {
    return vector_get(pool->constants_vec, (size_t)index);
}

int constant_pool_size(ConstantPool* pool) {
    return (int)vector_length(pool->constants_vec);
}

/* =================================================================
 * Lexical Environment Implementation
 * ================================================================= */

LexicalEnv* lexical_env_new(LexicalEnv* parent, bool is_function) {
    LexicalEnv* env = (LexicalEnv*)malloc(sizeof(LexicalEnv));
    if (!env) return NULL;

    env->parent = parent;
    env->bindings = hashmap_create_default();
    env->is_function = is_function;
    if (is_function) {
        env->base_index = 0;  /* Function boundary resets base (new stack frame) */
    } else {
        env->base_index = parent ? parent->base_index + (int)hashmap_size(parent->bindings) : 0;
    }

    return env;
}

void lexical_env_free(LexicalEnv* env) {
    if (env) {
        object_release(env->bindings);
        free(env);
    }
}

int lexical_env_add_local(LexicalEnv* env, Value symbol) {
    int local_idx = env->base_index + (int)hashmap_size(env->bindings);
    hashmap_set(env->bindings, symbol, make_fixnum(local_idx));
    return local_idx;
}

int lexical_env_lookup(LexicalEnv* env, Value symbol) {
    while (env != NULL) {
        Value idx_val = hashmap_get(env->bindings, symbol);
        if (!is_nil(idx_val)) {
            return (int)untag_fixnum(idx_val);
        }
        if (env->is_function) {
            /* Don't cross function boundaries for locals */
            break;
        }
        env = env->parent;
    }
    return -1;
}

/* =================================================================
 * Compiler Lifecycle
 * ================================================================= */

Compiler* compiler_new(const char* filename) {
    Compiler* c = (Compiler*)malloc(sizeof(Compiler));
    if (!c) return NULL;

    c->bytecode = bytecode_buffer_new();
    c->constants = constant_pool_new();
    c->env = NULL;  /* Will be created when compiling function */
    c->captures = NULL;
    c->recur_target = NULL;
    c->in_tail_pos = false;
    c->local_count = 0;
    c->error = false;
    c->error_msg[0] = '\0';
    c->filename = filename ? filename : "<unknown>";
    c->line = 1;
    c->column = 0;

    if (!c->bytecode || !c->constants) {
        compiler_free(c);
        return NULL;
    }

    return c;
}

void compiler_free(Compiler* c) {
    if (c) {
        bytecode_buffer_free(c->bytecode);
        constant_pool_free(c->constants);
        /* Note: env is freed as we pop scopes during compilation */
        free(c);
    }
}

void compiler_reset(Compiler* c) {
    if (!c) return;

    /* Free and recreate buffers */
    bytecode_buffer_free(c->bytecode);
    constant_pool_free(c->constants);

    c->bytecode = bytecode_buffer_new();
    c->constants = constant_pool_new();
    c->env = NULL;
    c->captures = NULL;
    c->recur_target = NULL;
    c->in_tail_pos = false;
    c->local_count = 0;
    c->error = false;
    c->error_msg[0] = '\0';
}

/* =================================================================
 * Error Handling
 * ================================================================= */

void compile_error(Compiler* c, const char* fmt, ...) {
    if (c->error) {
        return;  /* Already have an error */
    }

    c->error = true;

    va_list args;
    va_start(args, fmt);
    vsnprintf(c->error_msg, sizeof(c->error_msg), fmt, args);
    va_end(args);
}

bool compiler_has_error(Compiler* c) {
    return c ? c->error : false;
}

const char* compiler_error_msg(Compiler* c) {
    return c ? c->error_msg : "No compiler";
}

/* =================================================================
 * Emission Helpers
 * ================================================================= */

void emit_op(Compiler* c, uint8_t opcode) {
    bytecode_emit_byte(c->bytecode, opcode);
}

void emit_uint16(Compiler* c, uint8_t opcode, uint16_t operand) {
    bytecode_emit_byte(c->bytecode, opcode);
    bytecode_emit_uint16(c->bytecode, operand);
}

void emit_uint32(Compiler* c, uint8_t opcode, uint32_t operand) {
    bytecode_emit_byte(c->bytecode, opcode);
    bytecode_emit_uint32(c->bytecode, operand);
}

void emit_int32(Compiler* c, uint8_t opcode, int32_t operand) {
    bytecode_emit_byte(c->bytecode, opcode);
    /* int32 is encoded same as uint32, just reinterpret cast */
    bytecode_emit_uint32(c->bytecode, (uint32_t)operand);
}

void emit_int64(Compiler* c, uint8_t opcode, int64_t operand) {
    bytecode_emit_byte(c->bytecode, opcode);
    bytecode_emit_int64(c->bytecode, operand);
}

/* =================================================================
 * Compilation Helpers
 * ================================================================= */

/* Check if form is a symbol with given name */
static bool is_symbol_named(Value form, const char* name) {
    if (!is_pointer(form)) return false;
    if (object_type(form) != TYPE_SYMBOL) return false;
    return strcmp(symbol_name(form), name) == 0;
}


/* Get nth element of list (0-indexed) */
static Value list_nth_or_nil(Value list, int n) {
    for (int i = 0; i < n && is_cons(list); i++) {
        list = cdr(list);
    }
    return is_cons(list) ? car(list) : VALUE_NIL;
}

/* =================================================================
 * Forward Declarations
 * ================================================================= */

static void compile_literal(Compiler* c, Value form);
static void compile_symbol(Compiler* c, Value form);
static void compile_list(Compiler* c, Value form, bool in_tail_pos);
static void compile_vector_literal(Compiler* c, Value form);
static void compile_hashmap_literal(Compiler* c, Value form);
static void compile_if(Compiler* c, Value form, bool in_tail_pos);
static void compile_do(Compiler* c, Value form, bool in_tail_pos);
static void compile_def(Compiler* c, Value form);
static void compile_defmacro(Compiler* c, Value form);
static void compile_fn(Compiler* c, Value form);
static void compile_let(Compiler* c, Value form, bool in_tail_pos);
static void compile_loop(Compiler* c, Value form, bool in_tail_pos);
static void compile_recur(Compiler* c, Value form, bool in_tail_pos);
static void compile_throw(Compiler* c, Value form);
static void compile_try(Compiler* c, Value form, bool in_tail_pos);

/* =================================================================
 * Main Compilation Entry Point
 * ================================================================= */

void compile_expr(Compiler* c, Value form, bool in_tail_pos) {
    if (c->error) return;

    c->in_tail_pos = in_tail_pos;

    /* Nil, true, false */
    if (is_nil(form) || is_true(form) || is_false(form)) {
        compile_literal(c, form);
        return;
    }

    /* Fixnum, float, character */
    if (is_fixnum(form) || is_float(form) || is_char(form)) {
        compile_literal(c, form);
        return;
    }

    /* Heap objects */
    if (is_pointer(form)) {
        int type = object_type(form);

        switch (type) {
            case TYPE_BIGINT:
            case TYPE_STRING:
            case TYPE_KEYWORD:
                compile_literal(c, form);
                return;

            case TYPE_SYMBOL:
                compile_symbol(c, form);
                return;

            case TYPE_CONS:
                compile_list(c, form, in_tail_pos);
                return;

            case TYPE_VECTOR:
                compile_vector_literal(c, form);
                return;

            case TYPE_HASHMAP:
                compile_hashmap_literal(c, form);
                return;

            default:
                compile_error(c, "Cannot compile value of type %d", type);
                return;
        }
    }

    compile_error(c, "Invalid form for compilation");
}

/* =================================================================
 * Literal Compilation
 * ================================================================= */

static void compile_literal(Compiler* c, Value literal) {
    if (is_nil(literal)) {
        emit_op(c, OP_PUSH_NIL);
    } else if (is_true(literal)) {
        emit_op(c, OP_PUSH_TRUE);
    } else if (is_false(literal)) {
        emit_op(c, OP_PUSH_FALSE);
    } else if (is_fixnum(literal)) {
        int64_t n = untag_fixnum(literal);
        if (n >= INT64_MIN && n <= INT64_MAX) {
            emit_int64(c, OP_PUSH_INT, n);
        } else {
            /* Shouldn't happen for fixnums, but handle it */
            int idx = constant_pool_add(c->constants, literal);
            emit_uint32(c, OP_PUSH_CONST, (uint32_t)idx);
        }
    } else {
        /* Other literals: bigint, string, keyword, etc. */
        int idx = constant_pool_add(c->constants, literal);
        emit_uint32(c, OP_PUSH_CONST, (uint32_t)idx);
    }
}

/* =================================================================
 * Closure Capture Lookup
 * ================================================================= */

/* Ensure a symbol is captured in a given capture list.
 * Returns the capture index. Adds a new entry if not already present. */
static int ensure_capture(CaptureList* captures, Value symbol,
                          bool from_closure, int parent_idx) {
    /* Check if already captured */
    for (int i = 0; i < captures->count; i++) {
        if (value_equal(captures->entries[i].symbol, symbol)) {
            return i;
        }
    }

    if (captures->count >= MAX_CAPTURES) {
        return -1;  /* caller should report error */
    }

    int idx = captures->count;
    captures->entries[idx].symbol = symbol;
    captures->entries[idx].from_closure = from_closure;
    captures->entries[idx].parent_idx = parent_idx;
    captures->count++;
    return idx;
}

/* Look up a symbol in parent scopes beyond the current function boundary.
 * If found, add to the capture list and return the closure index.
 * For nested closures, ensures intermediate functions also capture the variable.
 * Returns -1 if not found in any enclosing scope. */
static int capture_lookup(Compiler* c, Value symbol) {
    if (!c->captures) return -1;

    /* Check if we already captured this symbol */
    for (int i = 0; i < c->captures->count; i++) {
        if (value_equal(c->captures->entries[i].symbol, symbol)) {
            return i;
        }
    }

    /* Walk up from the current env to find the function boundary,
     * then continue past it to look in parent scopes */
    LexicalEnv* env = c->env;

    /* Find the function boundary (the is_function env for the current fn) */
    while (env && !env->is_function) {
        env = env->parent;
    }
    if (!env) return -1;  /* No function boundary found */

    /* Now env is the function boundary. Walk past it into parent scopes.
     * Track how many function boundaries we cross to determine from_closure. */
    LexicalEnv* parent_env = env->parent;
    int fn_boundaries_crossed = 0;
    int found_local_idx = -1;

    while (parent_env != NULL) {
        Value idx_val = hashmap_get(parent_env->bindings, symbol);
        if (!is_nil(idx_val)) {
            found_local_idx = (int)untag_fixnum(idx_val);
            break;
        }
        if (parent_env->is_function) {
            fn_boundaries_crossed++;
        }
        parent_env = parent_env->parent;
    }

    if (found_local_idx < 0) return -1;  /* Not found in any enclosing scope */

    if (fn_boundaries_crossed == 0) {
        /* Variable is in the immediate parent function's scope.
         * from_closure=false, parent_idx = local index in parent. */
        int capture_idx = ensure_capture(c->captures, symbol, false, found_local_idx);
        if (capture_idx < 0) {
            compile_error(c, "Too many captured variables (max %d)", MAX_CAPTURES);
        }
        return capture_idx;
    } else {
        /* Variable is past another function boundary. The immediate parent
         * function must also capture this variable so we can access it
         * via OP_LOAD_CLOSURE from the parent's closed[] array.
         *
         * We use a simpler approach: since compile_fn saves/restores
         * parent captures, we can directly add to the parent's capture list
         * here. The parent's capture-push code will be emitted when the
         * parent's compile_fn finishes (after our body compilation returns).
         *
         * Walk the capture lists up to ensure each intermediate function
         * captures the variable, building a chain. */

        /* We need to find the parent's capture list. It was saved before
         * we started compiling the current fn. We stored it in parent_captures.
         * But we don't have direct access to it from here...
         *
         * Instead, we use a different approach: since compilation is recursive
         * and each fn's compile_fn restores parent captures, we can look at
         * what the parent would see. If the parent is also a closure, the
         * variable would need to be in the parent's captures.
         *
         * The key insight: when we finish compiling this inner fn, the parent's
         * compile_fn continues and will emit the capture-push code. At that
         * point, parent's compile_symbol would have been the one to capture
         * the var if it appeared in the parent's own body. But since it only
         * appears in the inner fn's body, we need to manually add it to
         * the parent's capture list.
         *
         * We walk up the saved capture lists to propagate.
         * For now: just record from_closure=true with the original local idx.
         * The capture-push code in compile_fn will emit OP_LOAD_CLOSURE. */

        /* Find the parent capture list by walking the compiler's saved state.
         * Since we can't easily access it here, let's use a simpler approach:
         * The parent's CaptureList was saved to parent_captures before compile_fn
         * set c->captures. We can pass this through compile_fn. */

        /* SIMPLIFIED approach for nested closures:
         * We record from_closure=true and parent_idx = the local index in the
         * original scope. But the capture-push code needs an index that works
         * in the parent's context.
         *
         * Since the parent might be a plain function (no captures yet) or a
         * closure itself, and we can't easily propagate here, let's keep it
         * simple: we record from_closure=true and found_local_idx.
         *
         * In compile_fn's capture-push, for from_closure=true entries, we'll
         * need to look up the variable in the parent's capture list.
         * But we CAN do that because at that point c->captures points to
         * the parent's capture list. */

        /* Actually, the cleanest approach: don't try to propagate here.
         * Instead, store from_closure=true with the ORIGINAL local index.
         * In compile_fn, when emitting capture-push code, for from_closure
         * entries, we look up the variable in the now-current (parent's)
         * capture list, adding it if needed. This works because by the time
         * we emit capture-push code in compile_fn, c->captures has been
         * restored to the parent's list. */

        int capture_idx = ensure_capture(c->captures, symbol, true, found_local_idx);
        if (capture_idx < 0) {
            compile_error(c, "Too many captured variables (max %d)", MAX_CAPTURES);
        }
        return capture_idx;
    }
}

/* =================================================================
 * Vector Literal Compilation: [1 2 3] → (vector 1 2 3)
 * ================================================================= */

static void compile_vector_literal(Compiler* c, Value form) {
    size_t n = vector_length(form);

    /* Compile each element */
    for (size_t i = 0; i < n; i++) {
        compile_expr(c, vector_get(form, i), false);
        if (c->error) return;
    }

    /* Load the 'vector' function from namespace */
    Value vec_sym = symbol_intern("vector");
    int idx = constant_pool_add(c->constants, vec_sym);
    emit_uint16(c, OP_LOAD_VAR, (uint16_t)idx);

    emit_uint16(c, OP_CALL, (uint16_t)n);
}

/* =================================================================
 * HashMap Literal Compilation: {:a 1 :b 2} → (hash-map :a 1 :b 2)
 * ================================================================= */

static void compile_hashmap_literal(Compiler* c, Value form) {
    Value entries = hashmap_entries(form);
    size_t n_entries = vector_length(entries);

    /* Compile each key-value pair */
    for (size_t i = 0; i < n_entries; i++) {
        Value entry = vector_get(entries, i);  /* [key value] vector */
        compile_expr(c, vector_get(entry, 0), false);  /* key */
        if (c->error) { object_release(entries); return; }
        compile_expr(c, vector_get(entry, 1), false);  /* value */
        if (c->error) { object_release(entries); return; }
    }

    object_release(entries);

    /* Load the 'hash-map' function from namespace */
    Value hm_sym = symbol_intern("hash-map");
    int idx = constant_pool_add(c->constants, hm_sym);
    emit_uint16(c, OP_LOAD_VAR, (uint16_t)idx);

    emit_uint16(c, OP_CALL, (uint16_t)(n_entries * 2));
}

/* =================================================================
 * Symbol Compilation
 * ================================================================= */

static void compile_symbol(Compiler* c, Value symbol) {
    /* Check for local binding */
    int local_idx = lexical_env_lookup(c->env, symbol);
    if (local_idx >= 0) {
        emit_uint16(c, OP_LOAD_LOCAL, (uint16_t)local_idx);
        return;
    }

    /* Check for captured variable (closure) */
    int capture_idx = capture_lookup(c, symbol);
    if (capture_idx >= 0) {
        emit_uint16(c, OP_LOAD_CLOSURE, (uint16_t)capture_idx);
        return;
    }

    /* Global var lookup */
    int symbol_idx = constant_pool_add(c->constants, symbol);
    emit_uint16(c, OP_LOAD_VAR, (uint16_t)symbol_idx);
}

/* =================================================================
 * Quasiquote Expansion
 * ================================================================= */

/* Helper: check if form is (sym_name x) */
static bool is_tagged_list(Value form, const char* sym_name) {
    if (!is_cons(form)) return false;
    Value head = car(form);
    return is_pointer(head) && object_type(head) == TYPE_SYMBOL
        && strcmp(symbol_name(head), sym_name) == 0;
}

/* Expand quasiquoted form into list/concat/quote calls.
 * Returns a new Value (S-expression) to be compiled normally. */
static Value quasiquote_expand(Value form) {
    /* (unquote x) -> x */
    if (is_tagged_list(form, "unquote")) {
        return list_nth_or_nil(form, 1);
    }

    /* Atom (non-list) -> (quote atom) */
    if (!is_cons(form)) {
        Value quote_sym = symbol_intern("quote");
        return cons(quote_sym, cons(form, VALUE_NIL));
    }

    /* (unquote-splicing x) at top level -> error (handled by caller) */

    /* List: process each element, build (concat seg1 seg2 ...) */
    Value concat_sym = symbol_intern("concat");
    Value list_sym = symbol_intern("list");

    /* Build segments in reverse, then construct the concat call */
    Value segments = VALUE_NIL;  /* list of segments to concat */
    Value cur = form;

    while (is_cons(cur)) {
        Value elem = car(cur);

        if (is_tagged_list(elem, "unquote-splicing")) {
            /* ~@x -> x itself is a segment */
            Value spliced = list_nth_or_nil(elem, 1);
            segments = cons(spliced, segments);
        } else {
            /* anything else -> (list (qq-expand elem)) */
            Value expanded = quasiquote_expand(elem);
            Value wrapped = cons(list_sym, cons(expanded, VALUE_NIL));
            segments = cons(wrapped, segments);
        }

        cur = cdr(cur);
    }

    /* Reverse segments list */
    Value reversed = VALUE_NIL;
    while (is_cons(segments)) {
        Value seg = car(segments);
        reversed = cons(seg, reversed);
        segments = cdr(segments);
    }

    /* Count segments */
    int seg_count = 0;
    cur = reversed;
    while (is_cons(cur)) {
        seg_count++;
        cur = cdr(cur);
    }

    /* If only one segment, return it directly (skip concat) */
    if (seg_count == 1) {
        return car(reversed);
    }

    /* Build (concat seg1 seg2 ...) */
    return cons(concat_sym, reversed);
}

/* =================================================================
 * Macro Expansion (compile-time execution)
 * ================================================================= */

/* Invoke a macro function at compile time with unevaluated arg forms.
 * Returns the expanded form (a new Value). Returns VALUE_NIL on error. */
static Value expand_macro(Compiler* c, Value macro_fn, Value form) {
    /* Collect unevaluated arg forms */
    Value arg_forms = cdr(form);  /* skip the macro name */
    int n_args = 0;
    Value cur = arg_forms;
    while (is_cons(cur)) {
        n_args++;
        cur = cdr(cur);
    }

    /* Build mini constant pool: [arg0, arg1, ..., macro_fn] */
    int n_consts = n_args + 1;
    Value* consts = malloc(sizeof(Value) * (size_t)n_consts);
    cur = arg_forms;
    for (int i = 0; i < n_args; i++) {
        consts[i] = car(cur);
        cur = cdr(cur);
    }
    consts[n_args] = macro_fn;

    /* Build mini bytecode: PUSH_CONST for each arg, PUSH_CONST fn, CALL n, HALT */
    size_t code_size = (size_t)(n_consts * 5 + 3 + 1);  /* 5 bytes per PUSH_CONST, 3 for CALL, 1 for HALT */
    uint8_t* code = malloc(code_size);
    size_t pc = 0;

    /* Push args */
    for (int i = 0; i < n_args; i++) {
        code[pc++] = OP_PUSH_CONST;
        code[pc++] = (uint8_t)(i & 0xFF);
        code[pc++] = (uint8_t)((i >> 8) & 0xFF);
        code[pc++] = (uint8_t)((i >> 16) & 0xFF);
        code[pc++] = (uint8_t)((i >> 24) & 0xFF);
    }

    /* Push macro fn */
    code[pc++] = OP_PUSH_CONST;
    code[pc++] = (uint8_t)(n_args & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 8) & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 16) & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 24) & 0xFF);

    /* CALL n_args */
    code[pc++] = OP_CALL;
    code[pc++] = (uint8_t)(n_args & 0xFF);
    code[pc++] = (uint8_t)((n_args >> 8) & 0xFF);

    /* HALT */
    code[pc++] = OP_HALT;

    /* Run in temporary VM */
    VM* vm = vm_new(256);
    vm_load_code(vm, code, (int)pc);
    vm_load_constants(vm, consts, n_consts);
    vm_run(vm);

    Value result = VALUE_NIL;
    if (vm->error) {
        compile_error(c, "macro expansion failed: %s", vm->error_msg);
    } else if (vm->stack_pointer > 0) {
        result = vm->stack[vm->stack_pointer - 1];
        /* Retain result before VM is freed */
        if (is_pointer(result)) object_retain(result);
    }

    vm_free(vm);
    free(code);
    free(consts);
    return result;
}

/* =================================================================
 * List Compilation (Function Calls and Special Forms)
 * ================================================================= */

static void compile_list(Compiler* c, Value form, bool in_tail_pos) {
    if (!is_cons(form)) {
        compile_error(c, "Expected list");
        return;
    }

    Value first = car(form);

    /* Empty list */
    if (is_nil(first)) {
        compile_error(c, "Cannot compile empty list ()");
        return;
    }

    /* Check for special forms */
    if (is_pointer(first) && object_type(first) == TYPE_SYMBOL) {
        const char* name = symbol_name(first);

        if (strcmp(name, "if") == 0) {
            compile_if(c, form, in_tail_pos);
            return;
        } else if (strcmp(name, "do") == 0) {
            compile_do(c, form, in_tail_pos);
            return;
        } else if (strcmp(name, "quote") == 0) {
            /* (quote form) => push form as literal */
            Value quoted = list_nth_or_nil(form, 1);
            compile_literal(c, quoted);
            return;
        } else if (strcmp(name, "quasiquote") == 0) {
            /* (quasiquote form) => expand and compile */
            Value quoted = list_nth_or_nil(form, 1);
            Value expanded = quasiquote_expand(quoted);
            compile_expr(c, expanded, in_tail_pos);
            return;
        } else if (strcmp(name, "def") == 0) {
            /* (def symbol value) => define global var */
            compile_def(c, form);
            return;
        } else if (strcmp(name, "defmacro") == 0) {
            compile_defmacro(c, form);
            return;
        } else if (strcmp(name, "fn") == 0) {
            /* (fn [params] body...) or (fn name [params] body...) */
            compile_fn(c, form);
            return;
        } else if (strcmp(name, "let*") == 0) {
            /* (let* [bindings] body...) */
            compile_let(c, form, in_tail_pos);
            return;
        } else if (strcmp(name, "loop") == 0) {
            /* (loop [bindings] body...) */
            compile_loop(c, form, in_tail_pos);
            return;
        } else if (strcmp(name, "recur") == 0) {
            /* (recur args...) */
            compile_recur(c, form, in_tail_pos);
            return;
        } else if (strcmp(name, "throw") == 0) {
            compile_throw(c, form);
            return;
        } else if (strcmp(name, "try") == 0) {
            compile_try(c, form, in_tail_pos);
            return;
        } else if (strcmp(name, "spawn") == 0) {
            /* (spawn f arg1 arg2 ...) */
            Value args = cdr(form);
            int n_args = 0;

            /* Compile all args first (they go below fn on stack) */
            Value rest = cdr(args);  /* skip fn, compile regular args */
            while (is_cons(rest)) {
                compile_expr(c, car(rest), false);
                n_args++;
                rest = cdr(rest);
            }

            /* Compile function expression */
            compile_expr(c, car(args), false);

            emit_uint16(c, OP_SPAWN, (uint16_t)n_args);
            return;
        } else if (strcmp(name, "yield") == 0) {
            /* (yield) */
            emit_op(c, OP_YIELD);
            return;
        } else if (strcmp(name, "await") == 0) {
            /* (await task-expr) */
            Value task_expr = list_nth_or_nil(form, 1);
            compile_expr(c, task_expr, false);
            emit_op(c, OP_AWAIT);
            return;
        } else if (strcmp(name, ">!") == 0) {
            /* (>! ch val) */
            Value ch_expr = list_nth_or_nil(form, 1);
            Value val_expr = list_nth_or_nil(form, 2);
            compile_expr(c, ch_expr, false);
            compile_expr(c, val_expr, false);
            emit_op(c, OP_CHAN_SEND);
            return;
        } else if (strcmp(name, "<!") == 0) {
            /* (<! ch) */
            Value ch_expr = list_nth_or_nil(form, 1);
            compile_expr(c, ch_expr, false);
            emit_op(c, OP_CHAN_RECV);
            return;
        }

        /* Check if head symbol resolves to a macro */
        if (global_namespace_registry) {
            Namespace* ns = namespace_registry_current(global_namespace_registry);
            if (ns) {
                Var* var = NULL;

                if (symbol_has_namespace(first)) {
                    /* Qualified symbol: t/deftest — resolve via alias */
                    const char* sym_str = symbol_str(first);
                    const char* slash = strchr(sym_str, '/');
                    if (slash) {
                        size_t ns_len = (size_t)(slash - sym_str);
                        char ns_name[256];
                        if (ns_len < sizeof(ns_name)) {
                            memcpy(ns_name, sym_str, ns_len);
                            ns_name[ns_len] = '\0';
                            Value alias_sym = symbol_intern(ns_name);
                            const char* resolved = namespace_resolve_alias(ns, alias_sym);
                            const char* target_name = resolved ? resolved : ns_name;
                            Namespace* target_ns = namespace_registry_get(global_namespace_registry, target_name);
                            if (target_ns) {
                                const char* name_part = slash + 1;
                                Value name_sym = symbol_intern(name_part);
                                var = namespace_lookup(target_ns, name_sym);
                            }
                        }
                    }
                } else {
                    /* Unqualified: check current ns, then beer.core */
                    var = namespace_lookup(ns, first);
                    if (!var) {
                        Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
                        if (core_ns && core_ns != ns) {
                            var = namespace_lookup(core_ns, first);
                        }
                    }
                }

                if (var && var->is_macro) {
                    Value macro_fn = var_get_value(var);
                    Value expanded = expand_macro(c, macro_fn, form);
                    if (c->error) return;
                    compile_expr(c, expanded, in_tail_pos);
                    return;
                }
            }
        }
    }

    /* Function call: (f arg1 arg2 ...) */
    /* Compile arguments first (left to right) */
    Value args = cdr(form);
    int n_args = 0;

    while (is_cons(args)) {
        compile_expr(c, car(args), false);  /* Args not in tail position */
        n_args++;
        args = cdr(args);
    }

    /* Compile function expression */
    compile_expr(c, first, false);

    /* Emit call instruction */
    if (in_tail_pos) {
        emit_uint16(c, OP_TAIL_CALL, (uint16_t)n_args);
    } else {
        emit_uint16(c, OP_CALL, (uint16_t)n_args);
    }
}

/* =================================================================
 * Special Form: if
 * ================================================================= */

static void compile_if(Compiler* c, Value form, bool in_tail_pos) {
    /* (if test then else?) */
    Value test = list_nth_or_nil(form, 1);
    Value then_branch = list_nth_or_nil(form, 2);
    Value else_branch = list_nth_or_nil(form, 3);

    /* Compile test expression */
    compile_expr(c, test, false);

    /* Jump to else if false (JUMP_IF_FALSE peeks, doesn't pop) */
    size_t else_jump_offset = bytecode_current_offset(c->bytecode);
    emit_int32(c, OP_JUMP_IF_FALSE, 0);  /* Placeholder */

    /* Then branch: pop condition (it was truthy) */
    emit_op(c, OP_POP);

    /* Compile then branch */
    compile_expr(c, then_branch, in_tail_pos);

    /* Jump over else branch */
    size_t end_jump_offset = bytecode_current_offset(c->bytecode);
    emit_int32(c, OP_JUMP, 0);  /* Placeholder */

    /* Patch else jump (offset is relative to PC after reading the offset) */
    size_t else_target = bytecode_current_offset(c->bytecode);
    int32_t else_offset = (int32_t)(else_target - else_jump_offset - 5);  /* -5 = 1 byte opcode + 4 bytes operand */
    bytecode_patch_int32(c->bytecode, else_jump_offset + 1, else_offset);

    /* Else branch: pop condition (it was falsy) */
    emit_op(c, OP_POP);

    /* Compile else branch (nil if omitted) */
    if (is_nil(else_branch)) {
        emit_op(c, OP_PUSH_NIL);
    } else {
        compile_expr(c, else_branch, in_tail_pos);
    }

    /* Patch end jump (offset is relative to PC after reading the offset) */
    size_t end_target = bytecode_current_offset(c->bytecode);
    int32_t end_offset = (int32_t)(end_target - end_jump_offset - 5);  /* -5 = 1 byte opcode + 4 bytes operand */
    bytecode_patch_int32(c->bytecode, end_jump_offset + 1, end_offset);
}

/* =================================================================
 * Special Form: do
 * ================================================================= */

static void compile_do(Compiler* c, Value form, bool in_tail_pos) {
    /* (do expr1 expr2 ... exprN) */
    Value exprs = cdr(form);  /* Skip 'do' symbol */

    if (!is_cons(exprs)) {
        /* Empty do => nil */
        emit_op(c, OP_PUSH_NIL);
        return;
    }

    /* Compile all expressions */
    while (is_cons(exprs)) {
        Value expr = car(exprs);
        Value rest = cdr(exprs);

        /* Last expression inherits tail position */
        bool is_last = !is_cons(rest);

        compile_expr(c, expr, is_last && in_tail_pos);

        if (!is_last) {
            /* Not last => discard result */
            emit_op(c, OP_POP);
        }

        exprs = rest;
    }
}

/* =================================================================
 * Special Form: def
 * ================================================================= */

static void compile_def(Compiler* c, Value form) {
    /* (def symbol value) => define global var */
    Value symbol = list_nth_or_nil(form, 1);
    Value value_expr = list_nth_or_nil(form, 2);

    /* Validate symbol */
    if (!is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
        compile_error(c, "def: first argument must be a symbol");
        return;
    }

    /* Compile the value expression */
    compile_expr(c, value_expr, false);

    /* Add symbol to constant pool */
    int symbol_idx = constant_pool_add(c->constants, symbol);

    /* Emit STORE_VAR instruction */
    emit_uint16(c, OP_STORE_VAR, (uint16_t)symbol_idx);

    /* def returns the value (which is already on stack after STORE_VAR) */
}

/* =================================================================
 * Special Form: defmacro
 * ================================================================= */

static void compile_defmacro(Compiler* c, Value form) {
    /* (defmacro name [params] body...) */
    Value name = list_nth_or_nil(form, 1);

    if (!is_pointer(name) || object_type(name) != TYPE_SYMBOL) {
        compile_error(c, "defmacro: first argument must be a symbol");
        return;
    }

    /* Build (fn name [params] body...) and compile it */
    Value fn_form = cons(symbol_intern("fn"), cdr(form));  /* (fn name [params] body...) */
    compile_fn(c, fn_form);

    /* Store in namespace (like def) */
    int name_idx = constant_pool_add(c->constants, name);
    emit_uint16(c, OP_STORE_VAR, (uint16_t)name_idx);

    /* Now call (set-macro! name) to mark it as a macro */
    emit_op(c, OP_POP);  /* discard the fn value from STORE_VAR */

    /* Push the name symbol as argument for set-macro! */
    int sym_const_idx = constant_pool_add(c->constants, name);
    emit_uint32(c, OP_PUSH_CONST, (uint32_t)sym_const_idx);

    /* Load set-macro! function */
    Value setmacro_sym = symbol_intern("set-macro!");
    int setmacro_idx = constant_pool_add(c->constants, setmacro_sym);
    emit_uint16(c, OP_LOAD_VAR, (uint16_t)setmacro_idx);

    /* Call set-macro! with 1 arg */
    emit_uint16(c, OP_CALL, 1);

    /* Discard set-macro! return (nil), load the defined var as result */
    emit_op(c, OP_POP);
    emit_uint16(c, OP_LOAD_VAR, (uint16_t)name_idx);
}

/* =================================================================
 * Special Form: fn
 * ================================================================= */

static void compile_fn(Compiler* c, Value form) {
    /* Parse: (fn [params] body...) or (fn name [params] body...) */
    Value rest = cdr(form);  /* Skip 'fn' symbol */

    /* Check for named function */
    Value name = VALUE_NIL;
    Value params_vec = VALUE_NIL;
    Value body = VALUE_NIL;

    if (!is_cons(rest)) {
        compile_error(c, "fn: missing parameter vector");
        return;
    }

    Value first = car(rest);
    rest = cdr(rest);

    /* Is it a named function? (fn name [params] body...) */
    if (is_pointer(first) && object_type(first) == TYPE_SYMBOL) {
        name = first;
        if (!is_cons(rest)) {
            compile_error(c, "fn: missing parameter vector after name");
            return;
        }
        params_vec = car(rest);
        body = cdr(rest);
    } else {
        /* Anonymous function: (fn [params] body...) */
        params_vec = first;
        body = rest;
    }

    /* Validate parameter vector */
    if (!is_pointer(params_vec) || object_type(params_vec) != TYPE_VECTOR) {
        compile_error(c, "fn: parameter list must be a vector");
        return;
    }

    /* Validate body exists */
    if (!is_cons(body)) {
        compile_error(c, "fn: missing body");
        return;
    }

    /* Parse parameters, checking for & rest args */
    int arity;        /* call-site arity: negative for variadic */
    int total_params;  /* actual number of parameter locals */
    bool is_variadic = false;
    int required_count = 0;
    int ampersand_pos = -1;

    /* Scan for & symbol */
    for (int i = 0; i < (int)vector_length(params_vec); i++) {
        Value param = vector_get(params_vec, (size_t)i);
        if (is_pointer(param) && object_type(param) == TYPE_SYMBOL
            && strcmp(symbol_name(param), "&") == 0) {
            ampersand_pos = i;
            break;
        }
    }

    if (ampersand_pos >= 0) {
        is_variadic = true;
        required_count = ampersand_pos;
        /* Validate: & must be followed by exactly one symbol */
        if ((int)vector_length(params_vec) != ampersand_pos + 2) {
            compile_error(c, "fn: & must be followed by exactly one parameter");
            return;
        }
        Value rest_param = vector_get(params_vec, (size_t)(ampersand_pos + 1));
        if (!is_pointer(rest_param) || object_type(rest_param) != TYPE_SYMBOL) {
            compile_error(c, "fn: rest parameter after & must be a symbol");
            return;
        }
        arity = -(required_count + 1);
        total_params = required_count + 1;  /* required + rest */
    } else {
        required_count = (int)vector_length(params_vec);
        arity = required_count;
        total_params = required_count;
    }

    /* Emit JUMP to skip over function body (we'll patch this later) */
    size_t skip_jump_offset = bytecode_current_offset(c->bytecode);
    emit_int32(c, OP_JUMP, 0);  /* Placeholder (int32 offset) */

    /* Remember code offset where function starts */
    uint32_t fn_code_offset = (uint32_t)bytecode_current_offset(c->bytecode);

    /* Save parent environment and captures, set up new ones */
    LexicalEnv* parent_env = c->env;
    CaptureList* parent_captures = c->captures;
    CaptureList fn_captures = { .count = 0 };
    c->captures = &fn_captures;
    c->env = lexical_env_new(parent_env, true);  /* is_function = true */

    /* Add required parameters as locals */
    for (int i = 0; i < required_count; i++) {
        Value param = vector_get(params_vec, (size_t)i);
        if (!is_pointer(param) || object_type(param) != TYPE_SYMBOL) {
            compile_error(c, "fn: parameter must be a symbol");
            lexical_env_free(c->env);
            c->env = parent_env;
            c->captures = parent_captures;
            return;
        }
        lexical_env_add_local(c->env, param);
    }

    /* Add rest parameter if variadic */
    if (is_variadic) {
        Value rest_param = vector_get(params_vec, (size_t)(ampersand_pos + 1));
        lexical_env_add_local(c->env, rest_param);
    }

    /* For named functions, add the name as a local (for recursion) */
    if (!is_nil(name)) {
        lexical_env_add_local(c->env, name);
    }

    /* Save parent's local_count; each fn has its own local tracking */
    int saved_local_count = c->local_count;
    c->local_count = total_params + (is_nil(name) ? 0 : 1);

    /* Emit ENTER instruction with placeholder — we'll patch it after body compilation */
    size_t enter_operand_offset = bytecode_current_offset(c->bytecode);
    emit_uint16(c, OP_ENTER, 0);  /* Placeholder */

    /* For named functions, store the function into its self-reference local */
    if (!is_nil(name)) {
        int name_slot = total_params;  /* name is added after params */
        emit_op(c, OP_LOAD_SELF);
        emit_uint16(c, OP_STORE_LOCAL, (uint16_t)name_slot);
    }

    /* Set recur target so (recur ...) targets this fn's entry */
    RecurTarget* saved_recur_target = c->recur_target;
    RecurTarget fn_recur_target;
    fn_recur_target.jump_pc = (uint32_t)bytecode_current_offset(c->bytecode);
    fn_recur_target.first_binding_index = 0;  /* first param is local 0 */
    fn_recur_target.n_bindings = total_params;
    c->recur_target = &fn_recur_target;

    /* Compile function body (implicit do) */
    while (is_cons(body)) {
        Value expr = car(body);
        Value rest_body = cdr(body);
        bool is_last = !is_cons(rest_body);

        /* Last expression is in tail position (could be tail call) */
        compile_expr(c, expr, is_last);

        if (!is_last) {
            /* Not last => discard result */
            emit_op(c, OP_POP);
        }

        body = rest_body;
    }

    /* Emit RETURN */
    emit_op(c, OP_RETURN);

    /* Patch ENTER with the actual number of additional locals */
    int additional_locals = c->local_count - total_params;
    bytecode_patch_uint16(c->bytecode, enter_operand_offset + 1,
                          (uint16_t)additional_locals);

    /* n_locals for function object = total locals in this fn */
    int n_locals = c->local_count;

    /* Restore parent's local_count and recur target */
    c->local_count = saved_local_count;
    c->recur_target = saved_recur_target;

    /* Patch the skip jump (offset is relative to PC after reading the offset) */
    size_t after_fn = bytecode_current_offset(c->bytecode);
    int32_t skip_offset = (int32_t)(after_fn - skip_jump_offset - 5);  /* -5 = 1 byte opcode + 4 bytes operand */
    bytecode_patch_int32(c->bytecode, skip_jump_offset + 1, skip_offset);

    /* Snapshot captures before restoring parent */
    int n_closed = fn_captures.count;
    CaptureEntry captures_snapshot[MAX_CAPTURES];
    if (n_closed > 0) {
        memcpy(captures_snapshot, fn_captures.entries, n_closed * sizeof(CaptureEntry));
    }

    /* Restore parent environment and captures */
    lexical_env_free(c->env);
    c->env = parent_env;
    c->captures = parent_captures;

    /* Build function name */
    char fn_name_buf[256];
    if (!is_nil(name)) {
        snprintf(fn_name_buf, sizeof(fn_name_buf), "%s", symbol_name(name));
    } else {
        snprintf(fn_name_buf, sizeof(fn_name_buf), "anonymous-fn:%s", c->filename);
    }

    if (n_closed == 0) {
        /* Plain function — compile-time constant, no captures */
        Value fn_obj = function_new(arity, fn_code_offset, (uint16_t)n_locals, fn_name_buf);
        int fn_idx = constant_pool_add(c->constants, fn_obj);
        emit_uint32(c, OP_PUSH_CONST, (uint32_t)fn_idx);
        object_release(fn_obj);
    } else {
        /* Closure — emit code to push captured values, then MAKE_CLOSURE */
        /* Push each captured value in order (index 0 first, pushed first).
         * At this point c->captures and c->env have been restored to the
         * PARENT's context. */
        for (int i = 0; i < n_closed; i++) {
            CaptureEntry* entry = &captures_snapshot[i];
            if (entry->from_closure) {
                /* Variable is past another fn boundary. Check how the parent
                 * can access it: as a local or via its own closure.
                 * Use the parent's env/captures context (already restored). */
                int parent_local = lexical_env_lookup(c->env, entry->symbol);
                if (parent_local >= 0) {
                    /* Parent has it as a local */
                    emit_uint16(c, OP_LOAD_LOCAL, (uint16_t)parent_local);
                } else if (c->captures) {
                    /* Parent needs to capture it too — recurse via capture_lookup */
                    int parent_cap_idx = capture_lookup(c, entry->symbol);
                    if (parent_cap_idx < 0) {
                        compile_error(c, "Cannot resolve captured variable in parent scope");
                        return;
                    }
                    emit_uint16(c, OP_LOAD_CLOSURE, (uint16_t)parent_cap_idx);
                } else {
                    /* Parent is at top-level, try global — shouldn't normally happen */
                    emit_uint16(c, OP_LOAD_LOCAL, (uint16_t)entry->parent_idx);
                }
            } else {
                /* Parent accesses this as a local variable */
                emit_uint16(c, OP_LOAD_LOCAL, (uint16_t)entry->parent_idx);
            }
        }

        /* Add function name to constant pool for MAKE_CLOSURE */
        Value name_str = string_from_cstr(fn_name_buf);
        int name_idx = constant_pool_add(c->constants, name_str);
        object_release(name_str);

        /* Emit MAKE_CLOSURE: code_offset(u32) n_locals(u16) n_closed(u16) arity(i16) name_idx(u16) */
        bytecode_emit_byte(c->bytecode, OP_MAKE_CLOSURE);
        bytecode_emit_uint32(c->bytecode, fn_code_offset);
        bytecode_emit_uint16(c->bytecode, (uint16_t)n_locals);
        bytecode_emit_uint16(c->bytecode, (uint16_t)n_closed);
        bytecode_emit_uint16(c->bytecode, (uint16_t)(int16_t)arity);
        bytecode_emit_uint16(c->bytecode, (uint16_t)name_idx);
    }
}

/* =================================================================
 * Special Form: let
 * ================================================================= */

static void compile_let(Compiler* c, Value form, bool in_tail_pos) {
    /* Parse: (let* [bindings] body...) */
    Value rest = cdr(form);  /* Skip 'let*' symbol */

    if (!is_cons(rest)) {
        compile_error(c, "let*: missing binding vector");
        return;
    }

    Value bindings_vec = car(rest);
    Value body = cdr(rest);

    /* Validate bindings vector */
    if (!is_pointer(bindings_vec) || object_type(bindings_vec) != TYPE_VECTOR) {
        compile_error(c, "let*: bindings must be a vector");
        return;
    }

    /* Validate body exists */
    if (!is_cons(body)) {
        compile_error(c, "let*: missing body");
        return;
    }

    /* Check bindings are even (pairs of symbol/value) */
    size_t bindings_len = vector_length(bindings_vec);
    if (bindings_len % 2 != 0) {
        compile_error(c, "let*: bindings must be even (symbol/value pairs)");
        return;
    }


    /* Check if we're inside a function (need a frame for STORE_LOCAL/LOAD_LOCAL) */
    bool need_frame = true;
    for (LexicalEnv* env = c->env; env != NULL; env = env->parent) {
        if (env->is_function) {
            need_frame = false;  /* Already inside a function frame */
            break;
        }
    }

    /* If not inside a function, wrap let in a synthetic function and call it */
    size_t skip_jump_offset = 0;
    uint32_t fn_code_offset = 0;

    if (need_frame) {
        /* Emit JUMP to skip over function body */
        skip_jump_offset = bytecode_current_offset(c->bytecode);
        emit_int32(c, OP_JUMP, 0);

        /* Remember where function code starts */
        fn_code_offset = (uint32_t)bytecode_current_offset(c->bytecode);
    }

    /* Create new lexical environment */
    LexicalEnv* parent_env = c->env;
    c->env = lexical_env_new(parent_env, need_frame);  /* Mark as function boundary if wrapping */

    /* If wrapping in function, emit ENTER (placeholder, patched after bindings) */
    size_t enter_offset = 0;
    if (need_frame) {
        enter_offset = bytecode_current_offset(c->bytecode);
        emit_uint16(c, OP_ENTER, 0);  /* placeholder */
    }

    /* Process bindings in pairs */
    for (size_t i = 0; i < bindings_len; i += 2) {
        Value binding = vector_get(bindings_vec, i);
        Value value_expr = vector_get(bindings_vec, i + 1);

        if (!is_pointer(binding) || object_type(binding) != TYPE_SYMBOL) {
            compile_error(c, "let*: binding name must be a symbol");
            lexical_env_free(c->env);
            c->env = parent_env;
            return;
        }

        /* Simple binding: symbol = expr */
        compile_expr(c, value_expr, false);

        int local_idx = lexical_env_add_local(c->env, binding);
        emit_uint16(c, OP_STORE_LOCAL, (uint16_t)local_idx);

        if (local_idx >= c->local_count) {
            c->local_count = local_idx + 1;
        }
    }

    /* Patch ENTER with actual local count */
    if (need_frame) {
        int total_locals = c->local_count - c->env->base_index;
        bytecode_patch_uint16(c->bytecode, enter_offset + 1, (uint16_t)total_locals);
    }

    /* Compile body (implicit do) */
    while (is_cons(body)) {
        Value expr = car(body);
        Value rest_body = cdr(body);
        bool is_last = !is_cons(rest_body);

        /* Last expression: if we created a frame, it's NOT in tail position */
        bool expr_tail_pos = is_last && in_tail_pos && !need_frame;
        compile_expr(c, expr, expr_tail_pos);

        if (!is_last) {
            /* Not last => discard result */
            emit_op(c, OP_POP);
        }

        body = rest_body;
    }

    /* If we wrapped in function, emit RETURN and create function object */
    if (need_frame) {
        emit_op(c, OP_RETURN);

        /* Patch skip jump */
        size_t after_fn = bytecode_current_offset(c->bytecode);
        int32_t skip_offset = (int32_t)(after_fn - skip_jump_offset - 5);
        bytecode_patch_int32(c->bytecode, skip_jump_offset + 1, skip_offset);

        /* Create function object (arity=0, n_locals=actual count) */
        int total_locals = c->local_count - c->env->base_index;
        char let_name[256];
        snprintf(let_name, sizeof(let_name), "anonymous-fn:%s", c->filename);
        Value fn_obj = function_new(0, fn_code_offset, (uint16_t)total_locals, let_name);

        /* Add to constants */
        int fn_idx = constant_pool_add(c->constants, fn_obj);
        emit_uint32(c, OP_PUSH_CONST, (uint32_t)fn_idx);
        object_release(fn_obj);

        /* Call the function immediately with 0 args */
        emit_uint16(c, OP_CALL, 0);
    }

    /* Restore parent environment */
    lexical_env_free(c->env);
    c->env = parent_env;
}

/* =================================================================
 * Special Form: loop
 * ================================================================= */

static void compile_loop(Compiler* c, Value form, bool in_tail_pos) {
    /* Parse: (loop [bindings] body...) */
    Value rest = cdr(form);  /* Skip 'loop' symbol */

    // FIXME: this check appears to be missing something --
    // verify that it is correct -- I would expect
    // `!is_cons(car(rest))`
    if (!is_cons(rest)) {
        compile_error(c, "loop: missing binding vector");
        return;
    }

    Value bindings_vec = car(rest);
    Value body = cdr(rest);

    /* Validate bindings vector */
    if (!is_pointer(bindings_vec) || object_type(bindings_vec) != TYPE_VECTOR) {
        compile_error(c, "loop: bindings must be a vector");
        return;
    }

    /* Validate body exists */
    if (!is_cons(body)) {
        compile_error(c, "loop: missing body");
        return;
    }

    /* Check bindings are even (pairs of symbol/value) */
    size_t bindings_len = vector_length(bindings_vec);
    if (bindings_len % 2 != 0) {
        compile_error(c, "loop: bindings must be even (symbol/value pairs)");
        return;
    }

    int n_bindings = (int)(bindings_len / 2);

    /* Check if we're inside a function (need a frame for STORE_LOCAL/LOAD_LOCAL) */
    bool need_frame = true;
    for (LexicalEnv* env = c->env; env != NULL; env = env->parent) {
        if (env->is_function) {
            need_frame = false;
            break;
        }
    }

    /* If not inside a function, wrap in synthetic function */
    size_t skip_jump_offset = 0;
    uint32_t fn_code_offset = 0;
    int saved_local_count = 0;

    if (need_frame) {
        skip_jump_offset = bytecode_current_offset(c->bytecode);
        emit_int32(c, OP_JUMP, 0);
        fn_code_offset = (uint32_t)bytecode_current_offset(c->bytecode);

        saved_local_count = c->local_count;
        c->local_count = 0;
    }

    /* Create new lexical environment */
    LexicalEnv* parent_env = c->env;
    c->env = lexical_env_new(parent_env, need_frame);

    /* If wrapping in function, emit ENTER with placeholder */
    size_t enter_operand_offset = 0;
    if (need_frame) {
        enter_operand_offset = bytecode_current_offset(c->bytecode);
        emit_uint16(c, OP_ENTER, 0);  /* Placeholder */
    }

    /* Process bindings in pairs */
    int first_binding_index = -1;
    for (size_t i = 0; i < bindings_len; i += 2) {
        Value symbol = vector_get(bindings_vec, i);
        Value value_expr = vector_get(bindings_vec, i + 1);

        if (!is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
            compile_error(c, "loop: binding name must be a symbol");
            lexical_env_free(c->env);
            c->env = parent_env;
            return;
        }

        /* Compile the value expression */
        compile_expr(c, value_expr, false);

        /* Add symbol to environment and emit STORE_LOCAL */
        int local_idx = lexical_env_add_local(c->env, symbol);
        emit_uint16(c, OP_STORE_LOCAL, (uint16_t)local_idx);

        if (i == 0) {
            first_binding_index = local_idx;
        }

        /* Update max local count */
        if (local_idx >= c->local_count) {
            c->local_count = local_idx + 1;
        }
    }

    /* If no bindings, first_binding_index is just current count */
    if (first_binding_index < 0) {
        first_binding_index = c->local_count;
    }

    /* Set recur target to point here (start of loop body) */
    RecurTarget* saved_recur_target = c->recur_target;
    RecurTarget loop_recur_target;
    loop_recur_target.jump_pc = (uint32_t)bytecode_current_offset(c->bytecode);
    loop_recur_target.first_binding_index = first_binding_index;
    loop_recur_target.n_bindings = n_bindings;
    c->recur_target = &loop_recur_target;

    /* Compile body (implicit do) */
    while (is_cons(body)) {
        Value expr = car(body);
        Value rest_body = cdr(body);
        bool is_last = !is_cons(rest_body);

        bool expr_tail_pos = is_last && in_tail_pos && !need_frame;
        compile_expr(c, expr, expr_tail_pos || is_last);

        if (!is_last) {
            emit_op(c, OP_POP);
        }

        body = rest_body;
    }

    /* Restore recur target */
    c->recur_target = saved_recur_target;

    /* If we wrapped in function, emit RETURN and create function object */
    if (need_frame) {
        emit_op(c, OP_RETURN);

        /* Patch ENTER with actual locals */
        int additional_locals = c->local_count;
        bytecode_patch_uint16(c->bytecode, enter_operand_offset + 1,
                              (uint16_t)additional_locals);

        /* Patch skip jump */
        size_t after_fn = bytecode_current_offset(c->bytecode);
        int32_t skip_offset = (int32_t)(after_fn - skip_jump_offset - 5);
        bytecode_patch_int32(c->bytecode, skip_jump_offset + 1, skip_offset);

        /* Create function object */
        char loop_name[256];
        snprintf(loop_name, sizeof(loop_name), "anonymous-fn:%s", c->filename);
        Value fn_obj = function_new(0, fn_code_offset,
                                    (uint16_t)c->local_count, loop_name);

        int fn_idx = constant_pool_add(c->constants, fn_obj);
        emit_uint32(c, OP_PUSH_CONST, (uint32_t)fn_idx);
        object_release(fn_obj);

        emit_uint16(c, OP_CALL, 0);

        c->local_count = saved_local_count;
    }

    /* Restore parent environment */
    lexical_env_free(c->env);
    c->env = parent_env;
}

/* =================================================================
 * Special Form: recur
 * ================================================================= */

static void compile_recur(Compiler* c, Value form, bool in_tail_pos) {
    /* (recur arg1 arg2 ...) */
    if (!c->recur_target) {
        compile_error(c, "recur: not inside loop or fn");
        return;
    }

    if (!in_tail_pos) {
        compile_error(c, "recur: can only be used in tail position");
        return;
    }

    /* Count and compile arguments */
    Value args = cdr(form);
    int n_args = 0;

    /* First count args to validate */
    Value tmp = args;
    while (is_cons(tmp)) {
        n_args++;
        tmp = cdr(tmp);
    }

    if (n_args != c->recur_target->n_bindings) {
        compile_error(c, "recur: expected %d arguments, got %d",
                      c->recur_target->n_bindings, n_args);
        return;
    }

    /* Compile all argument expressions (left to right) */
    while (is_cons(args)) {
        compile_expr(c, car(args), false);
        args = cdr(args);
    }

    /* Store in reverse order (LIFO) to match binding indices */
    for (int i = n_args - 1; i >= 0; i--) {
        emit_uint16(c, OP_STORE_LOCAL,
                    (uint16_t)(c->recur_target->first_binding_index + i));
    }

    /* Jump back to loop/fn body start */
    size_t jump_offset = bytecode_current_offset(c->bytecode);
    int32_t offset = (int32_t)c->recur_target->jump_pc
                   - (int32_t)(jump_offset + 5);  /* 1 opcode + 4 offset */
    emit_int32(c, OP_JUMP, offset);
}

/* =================================================================
 * Special Form: throw
 * ================================================================= */

static void compile_throw(Compiler* c, Value form) {
    /* (throw expr) */
    Value expr = list_nth_or_nil(form, 1);
    compile_expr(c, expr, false);
    emit_op(c, OP_THROW);
}

/* =================================================================
 * Special Form: try/catch
 * ================================================================= */

/* Helper: compile finally body for side effects only.
 * Emits the finally expressions, then POP to discard result,
 * preserving the try/catch result underneath on the stack. */
static void compile_finally_body(Compiler* c, Value finally_body) {
    Value cur = finally_body;
    while (is_cons(cur)) {
        Value expr = car(cur);
        Value next = cdr(cur);
        compile_expr(c, expr, false);
        emit_op(c, OP_POP);  /* Discard each finally expr result */
        cur = next;
    }
}

static void compile_try(Compiler* c, Value form, bool in_tail_pos) {
    /* (try body... (catch e catch-body...) (finally finally-body...)) */
    Value rest = cdr(form);  /* Skip 'try' */

    /* Find catch and finally clauses, count body expressions */
    Value catch_clause = VALUE_NIL;
    Value finally_clause = VALUE_NIL;
    int body_count = 0;
    Value cur = rest;
    while (is_cons(cur)) {
        Value expr = car(cur);
        if (is_cons(expr) && is_symbol_named(car(expr), "catch")) {
            catch_clause = expr;
        } else if (is_cons(expr) && is_symbol_named(car(expr), "finally")) {
            finally_clause = expr;
        } else if (is_nil(catch_clause) && is_nil(finally_clause)) {
            body_count++;
        }
        cur = cdr(cur);
    }

    bool has_catch = !is_nil(catch_clause);
    bool has_finally = !is_nil(finally_clause);
    Value finally_body = has_finally ? cdr(finally_clause) : VALUE_NIL;

    if (!has_catch && !has_finally) {
        compile_error(c, "try: missing catch or finally clause");
        return;
    }

    /* Parse catch clause if present: (catch e body...) */
    Value catch_var = VALUE_NIL;
    Value catch_body = VALUE_NIL;
    if (has_catch) {
        Value catch_rest = cdr(catch_clause);  /* Skip 'catch' */
        if (!is_cons(catch_rest)) {
            compile_error(c, "try: catch requires a binding symbol");
            return;
        }
        catch_var = car(catch_rest);
        catch_body = cdr(catch_rest);

        if (!is_pointer(catch_var) || object_type(catch_var) != TYPE_SYMBOL) {
            compile_error(c, "try: catch binding must be a symbol");
            return;
        }
    }

    /* Check if we're inside a function (need a frame for catch variable) */
    bool need_frame = true;
    for (LexicalEnv* env = c->env; env != NULL; env = env->parent) {
        if (env->is_function) {
            need_frame = false;
            break;
        }
    }

    /* If not inside a function, wrap in synthetic function */
    size_t skip_jump_offset = 0;
    uint32_t fn_code_offset = 0;
    int saved_local_count = 0;
    size_t enter_operand_offset = 0;

    if (need_frame) {
        skip_jump_offset = bytecode_current_offset(c->bytecode);
        emit_int32(c, OP_JUMP, 0);
        fn_code_offset = (uint32_t)bytecode_current_offset(c->bytecode);
        saved_local_count = c->local_count;
        c->local_count = 0;
    }

    LexicalEnv* parent_env = c->env;
    if (need_frame) {
        c->env = lexical_env_new(parent_env, true);
        enter_operand_offset = bytecode_current_offset(c->bytecode);
        emit_uint16(c, OP_ENTER, 0);  /* Placeholder */
    }

    if (has_catch) {
        /* Emit OP_PUSH_HANDLER with placeholder catch_pc */
        bytecode_emit_byte(c->bytecode, OP_PUSH_HANDLER);
        size_t handler_operand_offset = bytecode_current_offset(c->bytecode);
        bytecode_emit_uint32(c->bytecode, 0);  /* Placeholder */

        /* Compile body expressions */
        if (body_count == 0) {
            emit_op(c, OP_PUSH_NIL);
        } else {
            cur = rest;
            int i = 0;
            while (i < body_count && is_cons(cur)) {
                compile_expr(c, car(cur), false);  /* Not tail pos inside try body */
                if (i < body_count - 1) {
                    emit_op(c, OP_POP);
                }
                i++;
                cur = cdr(cur);
            }
        }

        /* Normal exit: pop handler */
        emit_op(c, OP_POP_HANDLER);

        /* Emit finally body (first copy) if present */
        if (has_finally) {
            compile_finally_body(c, finally_body);
        }

        size_t jump_past_catch_offset = bytecode_current_offset(c->bytecode);
        emit_int32(c, OP_JUMP, 0);  /* Placeholder: jump past catch */

        /* Patch PUSH_HANDLER target → here (catch block start) */
        uint32_t catch_pc = (uint32_t)bytecode_current_offset(c->bytecode);
        bytecode_patch_uint32(c->bytecode, handler_operand_offset, catch_pc);

        /* In catch block: load exception, bind to catch var */
        LexicalEnv* try_env = c->env;
        c->env = lexical_env_new(try_env, false);

        emit_op(c, OP_LOAD_EXCEPTION);
        int catch_local = lexical_env_add_local(c->env, catch_var);
        emit_uint16(c, OP_STORE_LOCAL, (uint16_t)catch_local);
        if (catch_local >= c->local_count) {
            c->local_count = catch_local + 1;
        }

        /* Compile catch body */
        if (!is_cons(catch_body)) {
            emit_op(c, OP_PUSH_NIL);
        } else {
            cur = catch_body;
            while (is_cons(cur)) {
                Value expr = car(cur);
                Value next = cdr(cur);
                bool is_last = !is_cons(next);
                compile_expr(c, expr, is_last && in_tail_pos && !need_frame && !has_finally);
                if (!is_last) {
                    emit_op(c, OP_POP);
                }
                cur = next;
            }
        }

        /* Restore catch sub-environment */
        lexical_env_free(c->env);
        c->env = try_env;

        /* Emit finally body (second copy) if present */
        if (has_finally) {
            compile_finally_body(c, finally_body);
        }

        /* Patch jump past catch */
        size_t after_catch = bytecode_current_offset(c->bytecode);
        int32_t jump_offset = (int32_t)(after_catch - jump_past_catch_offset - 5);
        bytecode_patch_int32(c->bytecode, jump_past_catch_offset + 1, jump_offset);
    } else {
        /* try with finally only (no catch) — just compile body + finally */
        if (body_count == 0) {
            emit_op(c, OP_PUSH_NIL);
        } else {
            cur = rest;
            int i = 0;
            while (i < body_count && is_cons(cur)) {
                compile_expr(c, car(cur), false);
                if (i < body_count - 1) {
                    emit_op(c, OP_POP);
                }
                i++;
                cur = cdr(cur);
            }
        }

        compile_finally_body(c, finally_body);
    }

    /* If we wrapped in function, emit RETURN and create function object */
    if (need_frame) {
        emit_op(c, OP_RETURN);

        /* Patch ENTER */
        bytecode_patch_uint16(c->bytecode, enter_operand_offset + 1,
                              (uint16_t)c->local_count);

        /* Patch skip jump */
        size_t after_fn = bytecode_current_offset(c->bytecode);
        int32_t skip_offset = (int32_t)(after_fn - skip_jump_offset - 5);
        bytecode_patch_int32(c->bytecode, skip_jump_offset + 1, skip_offset);

        /* Create function object */
        char try_name[256];
        snprintf(try_name, sizeof(try_name), "anonymous-fn:%s", c->filename);
        Value fn_obj = function_new(0, fn_code_offset,
                                    (uint16_t)c->local_count, try_name);
        int fn_idx = constant_pool_add(c->constants, fn_obj);
        emit_uint32(c, OP_PUSH_CONST, (uint32_t)fn_idx);
        object_release(fn_obj);
        emit_uint16(c, OP_CALL, 0);

        c->local_count = saved_local_count;
        lexical_env_free(c->env);
        c->env = parent_env;
    }
}

/* =================================================================
 * Public Compilation API
 * ================================================================= */

CompiledCode* compile(Compiler* c, Value form) {
    if (!c) return NULL;

    /* Reset compiler state */
    compiler_reset(c);

    /* Create top-level environment */
    c->env = lexical_env_new(NULL, false);

    /* Compile the form */
    compile_expr(c, form, false);  /* Top-level is NOT in tail position */

    /* Add HALT at end */
    emit_op(c, OP_HALT);

    if (c->error) {
        lexical_env_free(c->env);
        c->env = NULL;
        return NULL;
    }

    /* Create result */
    CompiledCode* result = (CompiledCode*)malloc(sizeof(CompiledCode));
    if (!result) {
        lexical_env_free(c->env);
        c->env = NULL;
        return NULL;
    }

    /* Transfer ownership of bytecode */
    result->bytecode = c->bytecode->code;
    result->code_size = c->bytecode->length;
    result->constants = c->constants->constants_vec;
    result->n_locals = c->local_count;
    result->arity = -1;  /* Top-level code */

    /* Prevent double-free: nil out transferred vector, release dedup map */
    c->bytecode->code = NULL;
    c->constants->constants_vec = VALUE_NIL;
    object_release(c->constants->const_map);
    c->constants->const_map = VALUE_NIL;

    /* Clean up environment */
    lexical_env_free(c->env);
    c->env = NULL;

    return result;
}

void compiled_code_free(CompiledCode* code) {
    if (code) {
        free(code->bytecode);
        object_release(code->constants);
        free(code);
    }
}
