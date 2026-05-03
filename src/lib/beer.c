/* src/lib/beer.c — Embeddable C API implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "beer.h"
#include "log.h"
#include "memory.h"
#include "function.h"
#include "symbol.h"
#include "namespace.h"
#include "reader.h"
#include "compiler.h"
#include "task.h"
#include "scheduler.h"
#include "vm.h"
#include "bstring.h"
#include "cons.h"
#include "vector.h"
#include "hashmap.h"
#include "native.h"

/* Declared in core.c — keeps compiled code+constants alive so function
 * objects (which store raw pointers into them) remain valid. */
extern void core_retain_unit(CompiledCode* code, Value* constants);

/* Forward-declared in namespace.h */
extern NamespaceRegistry* global_namespace_registry;
extern Scheduler*         global_scheduler;

/* ------------------------------------------------------------------ */
/* BeerState                                                            */
/* ------------------------------------------------------------------ */

struct BeerState {
    char last_error[512];
};

/* ------------------------------------------------------------------ */
/* Internal: run one compiled form as a task                            */
/* ------------------------------------------------------------------ */

/* Run source through reader+compiler, execute each form as a task.
 * Returns 0 on success, 1 on error (error stored in B->last_error). */
static int beer_run_source(BeerState* B, const char* src,
                            const char* filename, Value* result_out) {
    if (result_out) *result_out = VALUE_NIL;

    Reader* reader = reader_new(src, filename);
    Value all_forms = reader_read_all(reader);
    if (reader_has_error(reader)) {
        snprintf(B->last_error, sizeof(B->last_error),
                 "read error: %s", reader_error_msg(reader));
        reader_free(reader);
        object_release(all_forms);
        return 1;
    }
    reader_free(reader);

    int error = 0;
    size_t n_forms = vector_length(all_forms);
    for (size_t fi = 0; fi < n_forms; fi++) {
        Value form = vector_get(all_forms, fi);
        Compiler* compiler = compiler_new(filename);
        CompiledCode* code = compile(compiler, form);
        if (compiler_has_error(compiler)) {
            snprintf(B->last_error, sizeof(B->last_error),
                     "compile error (form %zu): %s", fi + 1,
                     compiler_error_msg(compiler));
            compiled_code_free(code);
            compiler_free(compiler);
            error = 1;
            break;
        }
        compiler_free(compiler);

        /* Wire up function constants (same pattern as native_eval in core.c) */
        int n_constants = (int)vector_length(code->constants);
        Value* constants = malloc((size_t)n_constants * sizeof(Value));
        for (int i = 0; i < n_constants; i++) {
            constants[i] = vector_get(code->constants, i);
            if (is_pointer(constants[i])) object_retain(constants[i]);
        }
        for (int i = 0; i < n_constants; i++) {
            if (is_function(constants[i])) {
                function_set_code(constants[i], code->bytecode,
                                  (int)code->code_size, constants, n_constants);
                object_make_immortal(constants[i]);
            }
        }

        Value task_val = task_new_from_code(code->bytecode, (int)code->code_size,
                                             constants, n_constants, global_scheduler);
        Task* task = task_get(task_val);
        scheduler_run_task_to_completion(global_scheduler, task);

        if (task->vm->error) {
            snprintf(B->last_error, sizeof(B->last_error),
                     "%s", task->vm->error_msg ? task->vm->error_msg : "runtime error");
            error = 1;
            object_release(task_val);
            /* Keep code+constants alive even on error — function objects may
             * reference these buffers, and we must not free them. */
            core_retain_unit(code, constants);
            break;
        }

        /* Capture result of last form if requested */
        if (result_out && fi == n_forms - 1 && !vm_stack_empty(task->vm)) {
            *result_out = task->vm->stack[task->vm->stack_pointer - 1];
            beer_retain(*result_out);
        }

        object_release(task_val);
        /* Keep code+constants alive: function objects store raw pointers
         * into these buffers.  core_retain_unit owns them from here on. */
        core_retain_unit(code, constants);
    }

    object_release(all_forms);

    /* Drain any tasks spawned by the forms */
    if (!error && global_scheduler) {
        scheduler_run_until_done(global_scheduler);
    }

    return error;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

BeerState* beer_open(void) {
    log_init();
    memory_init();
    symbol_init();
    namespace_init();   /* idempotent; registers all natives + core.beer */

    BeerState* B = calloc(1, sizeof(BeerState));
    return B;
}

void beer_close(BeerState* B) {
    if (!B) return;
    namespace_shutdown();
    free(B);
}

/* ------------------------------------------------------------------ */
/* Load path                                                            */
/* ------------------------------------------------------------------ */

void beer_add_load_path(BeerState* B, const char* path) {
    (void)B;
    if (!path || !global_namespace_registry) return;

    Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
    if (!core_ns) return;

    Value lp_sym = symbol_intern("*load-path*");
    Var*  lp_var = namespace_lookup(core_ns, lp_sym);
    if (!lp_var) return;

    Value old_lp = var_get_value(lp_var);

    /* Build new vector: new path first, then existing entries */
    Value new_lp = vector_create(1 + vector_length(old_lp));
    size_t plen = strlen(path);
    char buf[512];
    if (plen > 0 && path[plen - 1] == '/') {
        snprintf(buf, sizeof(buf), "%s", path);
    } else {
        snprintf(buf, sizeof(buf), "%s/", path);
    }
    Value ps = string_from_cstr(buf);
    vector_push(new_lp, ps);
    object_release(ps);
    for (size_t i = 0; i < vector_length(old_lp); i++)
        vector_push(new_lp, vector_get(old_lp, i));

    var_set_value(lp_var, new_lp);
    object_release(new_lp);
}

/* ------------------------------------------------------------------ */
/* Eval                                                                 */
/* ------------------------------------------------------------------ */

int beer_do_string(BeerState* B, const char* src) {
    B->last_error[0] = '\0';
    return beer_run_source(B, src, "<string>", NULL);
}

int beer_do_file(BeerState* B, const char* path) {
    B->last_error[0] = '\0';
    FILE* f = fopen(path, "r");
    if (!f) {
        snprintf(B->last_error, sizeof(B->last_error),
                 "cannot open file: %s", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* src = malloc((size_t)sz + 1);
    if (!src) { fclose(f); return 1; }
    size_t n = fread(src, 1, (size_t)sz, f);
    src[n] = '\0';
    fclose(f);
    int result = beer_run_source(B, src, path, NULL);
    free(src);
    return result;
}

BeerValue beer_eval_expr(BeerState* B, const char* expr) {
    B->last_error[0] = '\0';
    Value result = VALUE_NIL;
    beer_run_source(B, expr, "<eval>", &result);
    /* Caller owns the retained ref; release it when done */
    return result;
}

/* ------------------------------------------------------------------ */
/* Lookup and call                                                      */
/* ------------------------------------------------------------------ */

BeerValue beer_lookup(BeerState* B, const char* qualified) {
    (void)B;
    if (!qualified || !global_namespace_registry) return VALUE_NIL;

    /* Split "ns/name" into namespace + name components */
    const char* slash = strchr(qualified, '/');
    Namespace* ns = NULL;
    const char* varname = NULL;

    if (slash) {
        char nsbuf[256];
        size_t nslen = (size_t)(slash - qualified);
        if (nslen >= sizeof(nsbuf)) return VALUE_NIL;
        memcpy(nsbuf, qualified, nslen);
        nsbuf[nslen] = '\0';
        ns = namespace_registry_get(global_namespace_registry, nsbuf);
        varname = slash + 1;
    } else {
        /* Try current namespace, then beer.core */
        ns = namespace_registry_current(global_namespace_registry);
        varname = qualified;
    }

    if (!ns) return VALUE_NIL;
    Value sym = symbol_intern(varname);
    Var* var = namespace_lookup(ns, sym);
    if (!var) {
        /* Fall back to beer.core */
        Namespace* core = namespace_registry_get_core(global_namespace_registry);
        if (core) var = namespace_lookup(core, sym);
    }
    if (!var) return VALUE_NIL;

    Value v = var_get_value(var);
    beer_retain(v);
    return v;
}

BeerValue beer_call(BeerState* B, BeerValue fn, int argc, BeerValue* argv) {
    B->last_error[0] = '\0';
    if (beer_is_nil(fn)) return VALUE_NIL;

    /* Build a tiny script: push args, push fn, CALL n, HALT.
     * Use the temp-VM pattern already established in the codebase. */

    /* We need fn + args as constants; build bytecode inline */
    int n_constants = argc + 1;   /* argv[0..argc-1] + fn */
    Value* constants = malloc((size_t)(n_constants) * sizeof(Value));
    for (int i = 0; i < argc; i++) constants[i] = argv[i];
    constants[argc] = fn;
    /* Retain each constant so the task owns them */
    for (int i = 0; i < n_constants; i++) beer_retain(constants[i]);

    /* Bytecode: PUSH_CONST for each arg, PUSH_CONST for fn, CALL argc, HALT */
    /* OP_PUSH_CONST = 0x13 (5 bytes each: opcode + uint32 index)
     * OP_CALL       = 0x63 (3 bytes: opcode + uint16 argc)
     * OP_HALT       = 0x6F (1 byte) */
    size_t code_size = (size_t)(n_constants) * 5 + 3 + 1;
    uint8_t* code = malloc(code_size);
    size_t p = 0;
    for (int i = 0; i < n_constants; i++) {
        code[p++] = 0x13;  /* OP_PUSH_CONST — 4-byte LITTLE-ENDIAN index */
        uint32_t idx = (uint32_t)i;
        code[p++] = (uint8_t)( idx        & 0xFF);
        code[p++] = (uint8_t)((idx >>  8) & 0xFF);
        code[p++] = (uint8_t)((idx >> 16) & 0xFF);
        code[p++] = (uint8_t)((idx >> 24) & 0xFF);
    }
    code[p++] = 0x63;  /* OP_CALL — 2-byte LITTLE-ENDIAN argc */
    code[p++] = (uint8_t)( argc        & 0xFF);
    code[p++] = (uint8_t)((argc >> 8)  & 0xFF);
    code[p++] = 0x6F;  /* OP_HALT */

    Value task_val = task_new_from_code(code, (int)code_size,
                                         constants, n_constants, global_scheduler);
    Task* task = task_get(task_val);
    scheduler_run_task_to_completion(global_scheduler, task);
    scheduler_run_until_done(global_scheduler);

    Value result = VALUE_NIL;
    if (task->vm->error) {
        snprintf(B->last_error, sizeof(B->last_error),
                 "%s", task->vm->error_msg ? task->vm->error_msg : "call error");
    } else if (!vm_stack_empty(task->vm)) {
        result = task->vm->stack[task->vm->stack_pointer - 1];
        beer_retain(result);
    }

    object_release(task_val);
    free(code);
    /* constants are owned by the task (task_new_from_code takes ownership) */

    return result;
}

/* ------------------------------------------------------------------ */
/* Native registration                                                  */
/* ------------------------------------------------------------------ */

void beer_register(BeerState* B, const char* ns, const char* name,
                   BeerNativeFn fn) {
    (void)B;
    if (!global_namespace_registry) return;
    Namespace* target = namespace_registry_get_or_create(
                            global_namespace_registry, ns);
    if (!target) return;
    Value fn_val = native_function_new(-1, (NativeFn)fn, name);
    Value sym    = symbol_intern(name);
    namespace_define(target, sym, fn_val);
    object_release(fn_val);
}

/* ------------------------------------------------------------------ */
/* Scheduler                                                            */
/* ------------------------------------------------------------------ */

void beer_run(BeerState* B) {
    (void)B;
    if (global_scheduler) scheduler_run_until_done(global_scheduler);
}

/* ------------------------------------------------------------------ */
/* Error handling                                                       */
/* ------------------------------------------------------------------ */

const char* beer_error(BeerState* B) {
    if (!B || B->last_error[0] == '\0') return NULL;
    return B->last_error;
}

void beer_clear_error(BeerState* B) {
    if (B) B->last_error[0] = '\0';
}

void beer_vm_error(BeerVM* vm, const char* msg) {
    vm_error((VM*)vm, msg);
}

/* ------------------------------------------------------------------ */
/* Value constructors                                                   */
/* ------------------------------------------------------------------ */

BeerValue beer_string(const char* s) {
    return string_from_cstr(s ? s : "");
}

BeerValue beer_keyword(const char* name) {
    return keyword_intern(name);
}

BeerValue beer_symbol(const char* name) {
    return symbol_intern(name);
}

/* ------------------------------------------------------------------ */
/* Value inspectors                                                     */
/* ------------------------------------------------------------------ */

const char* beer_to_cstring(BeerValue v) {
    if (is_pointer(v)) {
        int t = object_type(v);
        if (t == TYPE_STRING)  return string_cstr(v);
        if (t == TYPE_SYMBOL)  return symbol_str(v);
        if (t == TYPE_KEYWORD) return keyword_str(v);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Collection access                                                    */
/* ------------------------------------------------------------------ */

int beer_length(BeerValue v) {
    if (!is_pointer(v)) return -1;
    int t = object_type(v);
    if (t == TYPE_VECTOR)  return (int)vector_length(v);
    if (t == TYPE_CONS)    return (int)list_length(v);
    if (t == TYPE_HASHMAP) return -1;  /* use beer_get */
    if (t == TYPE_STRING)  return (int)vector_length(v); /* string char count */
    return -1;
}

BeerValue beer_nth(BeerValue v, int i) {
    if (!is_pointer(v) || i < 0) return VALUE_NIL;
    int t = object_type(v);
    if (t == TYPE_VECTOR) {
        if ((size_t)i >= vector_length(v)) return VALUE_NIL;
        return vector_get(v, (size_t)i);
    }
    if (t == TYPE_CONS) {
        Value cur = v;
        for (int n = 0; n < i; n++) {
            if (!is_pointer(cur) || object_type(cur) != TYPE_CONS)
                return VALUE_NIL;
            cur = cdr(cur);
        }
        if (!is_pointer(cur) || object_type(cur) != TYPE_CONS)
            return VALUE_NIL;
        return car(cur);
    }
    return VALUE_NIL;
}

BeerValue beer_get(BeerValue map, BeerValue key) {
    if (!is_pointer(map)) return VALUE_NIL;
    if (object_type(map) != TYPE_HASHMAP) return VALUE_NIL;
    return hashmap_get(map, key);
}
