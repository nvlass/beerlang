/* Beerlang REPL - Main entry point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include "beerlang.h"
#include "memory.h"
#include "symbol.h"
#include "namespace.h"
#include "reader.h"
#include "compiler.h"
#include "vm.h"
#include "value.h"
#include "function.h"
#include "hashmap.h"
#include "vector.h"
#include "bstring.h"
#include "scheduler.h"
#include "core.h"
#include "log.h"
#include "task.h"

#define INPUT_BUFFER_SIZE   4096
#define ACCUM_BUFFER_SIZE  65536   /* max accumulated multi-line input */
#define MAX_COMPILED_UNITS  1024

/* ----------------------------------------------------------------
 * is_form_complete — check whether SRC contains at least one
 * syntactically complete top-level form (balanced delimiters).
 * Accounts for strings and ; line comments.  Returns true when
 * depth == 0 and there is at least one non-whitespace character.
 * ---------------------------------------------------------------- */
static bool is_form_complete(const char* src) {
    int depth = 0;
    bool in_string = false;
    bool has_content = false;

    for (const char* p = src; *p; p++) {
        if (in_string) {
            if (*p == '\\' && *(p + 1)) {
                p++;   /* skip escape */
            } else if (*p == '"') {
                in_string = false;
            }
        } else {
            switch (*p) {
            case '"':
                in_string = true;
                has_content = true;
                break;
            case ';':                      /* line comment — skip to newline */
                while (*p && *p != '\n') p++;
                if (*p) p--;
                break;
            case '(':  case '[':  case '{':
                depth++;
                has_content = true;
                break;
            case ')':  case ']':  case '}':
                depth--;
                break;
            default:
                if (!isspace((unsigned char)*p))
                    has_content = true;
                break;
            }
        }
    }
    return has_content && (depth <= 0);
}

/* ================================================================
 * Shutdown helper
 * ================================================================ */

static void shutdown_systems(void) {
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
}

#ifdef BEER_TRACK_ALLOCS
static bool dump_leaks_flag = false;
static void maybe_dump_leaks(void) {
    if (dump_leaks_flag) memory_dump_objects();
}
#endif

/* ================================================================
 * Eval helper — compile and run a source string in the current ns
 * Returns 0 on success, 1 on error.
 * ================================================================ */

/* Kept-alive compiled units so function pointers stay valid */
static CompiledCode* compiled_units[MAX_COMPILED_UNITS];
static Value* constant_arrays[MAX_COMPILED_UNITS];
static int n_compiled_units = 0;

/* Evaluate a single form string. Returns 0 on success, 1 on error. */
static int eval_form(const char* source, const char* filename) {
    Reader* reader = reader_new(source, filename);
    Value form = reader_read(reader);

    if (reader_has_error(reader)) {
        fprintf(stderr, "Read error: %s\n", reader_error_msg(reader));
        reader_free(reader);
        return 1;
    }
    reader_free(reader);

    Compiler* compiler = compiler_new(filename);
    CompiledCode* code = compile(compiler, form);

    if (compiler_has_error(compiler)) {
        fprintf(stderr, "Compile error: %s\n", compiler_error_msg(compiler));
        compiled_code_free(code);
        compiler_free(compiler);
        object_release(form);
        return 1;
    }
    compiler_free(compiler);
    object_release(form);

    int n_constants = (int)vector_length(code->constants);
    Value* constants = malloc(n_constants * sizeof(Value));
    for (int i = 0; i < n_constants; i++) {
        constants[i] = vector_get(code->constants, i);
    }
    for (int i = 0; i < n_constants; i++) {
        if (is_function(constants[i])) {
            function_set_code(constants[i],
                              code->bytecode, (int)code->code_size,
                              constants, n_constants);
            object_make_immortal(constants[i]);
        }
    }

    Value task_val = task_new_from_code(code->bytecode, (int)code->code_size,
                                         constants, n_constants, global_scheduler);
    Task* task = task_get(task_val);
    scheduler_run_task_to_completion(global_scheduler, task);

    if (global_scheduler) {
        scheduler_run_until_done(global_scheduler);
    }

    int had_error = task->vm->error ? 1 : 0;
    if (task->vm->error) {
        fprintf(stderr, "Error: %s\n", task->vm->error_msg);
    }

    object_release(task_val);

    if (n_compiled_units < MAX_COMPILED_UNITS) {
        compiled_units[n_compiled_units] = code;
        constant_arrays[n_compiled_units] = constants;
        n_compiled_units++;
    } else {
        compiled_code_free(code);
        free(constants);
    }

    return had_error;
}

/* ================================================================
 * -e expression evaluation
 * ================================================================ */

/* Evaluate and print each expression from -e arguments, in order.
 * Uses the same pattern as the REPL: scheduler_run_task_to_completion
 * per form, then scheduler_run_until_done at the end. */
static int cmd_eval_exprs(const char** exprs, int n) {
    int result = 0;
    for (int ei = 0; ei < n; ei++) {
        Reader* reader = reader_new(exprs[ei], "-e");
        Value all_forms = reader_read_all(reader);
        if (reader_has_error(reader)) {
            fprintf(stderr, "beer -e: read error: %s\n", reader_error_msg(reader));
            reader_free(reader);
            object_release(all_forms);
            result = 1;
            break;
        }
        reader_free(reader);

        size_t n_forms = vector_length(all_forms);
        for (size_t fi = 0; fi < n_forms; fi++) {
            Value form = vector_get(all_forms, fi);
            Compiler* compiler = compiler_new("-e");
            CompiledCode* code = compile(compiler, form);
            if (compiler_has_error(compiler)) {
                fprintf(stderr, "beer -e: compile error: %s\n", compiler_error_msg(compiler));
                compiled_code_free(code);
                compiler_free(compiler);
                result = 1;
                break;
            }
            int n_constants = (int)vector_length(code->constants);
            Value* constants = malloc(n_constants * sizeof(Value));
            for (int i = 0; i < n_constants; i++) constants[i] = vector_get(code->constants, i);
            for (int i = 0; i < n_constants; i++) {
                if (is_function(constants[i])) {
                    function_set_code(constants[i], code->bytecode, (int)code->code_size,
                                      constants, n_constants);
                    object_make_immortal(constants[i]);
                }
            }
            Value task_val = task_new_from_code(code->bytecode, (int)code->code_size,
                                                constants, n_constants, global_scheduler);
            Task* task = task_get(task_val);
            scheduler_run_task_to_completion(global_scheduler, task);
            if (task->vm->error) {
                fprintf(stderr, "beer -e: %s\n", task->vm->error_msg);
                result = 1;
            } else if (!vm_stack_empty(task->vm)) {
                Value res = task->vm->stack[task->vm->stack_pointer - 1];
                if (is_pointer(res)) object_retain(res);
                vm_pop(task->vm);
                value_print_readable(res);
                printf("\n");
                if (is_pointer(res)) object_release(res);
            }
            object_release(task_val);
            if (n_compiled_units < MAX_COMPILED_UNITS) {
                compiled_units[n_compiled_units] = code;
                constant_arrays[n_compiled_units] = constants;
                n_compiled_units++;
            } else {
                compiled_code_free(code);
                free(constants);
            }
            compiler_free(compiler);
            if (result) break;
        }
        object_release(all_forms);
        if (result) break;
    }
    if (global_scheduler) scheduler_run_until_done(global_scheduler);
    return result;
}

/* ================================================================
 * beer.edn reading — used only for REPL startup path setup.
 * Subcommand implementations live in lib/tools/beer/tools.beer.
 * ================================================================ */

typedef struct {
    char paths[16][256];
    int n_paths;
} ProjectPaths;

static bool read_project_paths(ProjectPaths* out) {
    memset(out, 0, sizeof(ProjectPaths));

    FILE* f = fopen("beer.edn", "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = malloc(fsize + 1);
    if (!source) { fclose(f); return false; }
    fread(source, 1, fsize, f);
    source[fsize] = '\0';
    fclose(f);

    Reader* reader = reader_new(source, "beer.edn");
    Value form = reader_read(reader);
    bool ok = !reader_has_error(reader);
    reader_free(reader);
    free(source);

    if (!ok || !is_pointer(form) || object_type(form) != TYPE_HASHMAP) {
        if (ok) object_release(form);
        return false;
    }

    Value k_paths = keyword_intern("paths");
    Value v_paths = hashmap_get(form, k_paths);
    if (is_pointer(v_paths) && object_type(v_paths) == TYPE_VECTOR) {
        size_t n = vector_length(v_paths);
        if (n > 16) n = 16;
        for (size_t i = 0; i < n; i++) {
            Value p = vector_get(v_paths, i);
            if (is_string(p)) {
                snprintf(out->paths[out->n_paths], 256, "%s", string_cstr(p));
                out->n_paths++;
            }
        }
    }

    object_release(form);
    return true;
}

static void prepend_load_paths(ProjectPaths* pp) {
    Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
    if (!core_ns) return;

    Value lp_sym = symbol_intern("*load-path*");
    Var* lp_var = namespace_lookup(core_ns, lp_sym);
    if (!lp_var) return;

    Value old_lp = var_get_value(lp_var);
    Value new_lp = vector_create(pp->n_paths + (int)vector_length(old_lp));

    for (int i = 0; i < pp->n_paths; i++) {
        char buf[512];
        size_t len = strlen(pp->paths[i]);
        snprintf(buf, sizeof(buf), "%s%s", pp->paths[i],
                 (len > 0 && pp->paths[i][len-1] == '/') ? "" : "/");
        Value s = string_from_cstr(buf);
        vector_push(new_lp, s);
        object_release(s);
    }
    size_t n = vector_length(old_lp);
    for (size_t i = 0; i < n; i++) vector_push(new_lp, vector_get(old_lp, i));

    var_set_value(lp_var, new_lp);
    object_release(new_lp);
}

/* ================================================================
 * Subcommands — all delegate to beer.tools (lib/tools/beer/tools.beer)
 * ================================================================ */

static int cmd_new(const char* dir) {
    char expr[1024];
    snprintf(expr, sizeof(expr),
             "(do (require 'beer.tools) (beer.tools/new-project \"%s\"))", dir);
    return eval_form(expr, "<beer new>");
}

static int cmd_run(void) {
    return eval_form("(do (require 'beer.tools) (beer.tools/run))", "<beer run>");
}

static int cmd_build(void) {
    return eval_form("(do (require 'beer.tools) (beer.tools/build))", "<beer build>");
}

static int cmd_ubertar(void) {
    return eval_form("(do (require 'beer.tools) (beer.tools/ubertar))", "<beer ubertar>");
}

/* ================================================================
 * Subcommand: beer compile / beer check
 * ================================================================ */

static int cmd_compile(const char* path) {
    /* Default to current directory if no path given */
    const char* target = (path && path[0]) ? path : ".";
    char expr[2048];
    snprintf(expr, sizeof(expr),
             "(do (require 'beer.build)"
             "    (let [res (beer.build/compile-stale! \"%s\")]"
             "      (println (str \"Compiled: \" (:compiled res) \" file(s)\"))"
             "      (doseq [err (:errors res)]"
             "        (println (str \"ERROR: \" (:file err) \": \" (:error err))))"
             "      (if (empty? (:errors res)) 0 1)))",
             target);
    return eval_form(expr, "<beer compile>");
}

static int cmd_check(const char* path) {
    const char* target = (path && path[0]) ? path : ".";
    char expr[2048];
    snprintf(expr, sizeof(expr),
             "(do (require 'beer.build)"
             "    (beer.build/check-report \"%s\"))",
             target);
    return eval_form(expr, "<beer check>");
}


/* ================================================================
 * REPL mode
 * ================================================================ */

static int run_repl(void) {
    char input[INPUT_BUFFER_SIZE];
    char accum[ACCUM_BUFFER_SIZE];
    size_t accum_len = 0;
    accum[0] = '\0';

    printf("Beerlang v%d.%d.%d\n",
           BEERLANG_VERSION_MAJOR,
           BEERLANG_VERSION_MINOR,
           BEERLANG_VERSION_PATCH);
    printf("Type (exit) to quit\n\n");

    int line_number = 1;

    while (true) {
        Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
        if (accum_len == 0) {
            printf("%s:%d> ", cur_ns ? cur_ns->name : "beerlang", line_number);
        } else {
            printf("...   ");
        }
        fflush(stdout);

        /* Poll stdin with a short timeout so background tasks (accept-loop,
         * connection readers, actors) get CPU time while we wait for input.
         * Without this, spawned tasks are frozen between REPL prompts. */
        {
            bool got_input = false;
            while (!got_input) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(STDIN_FILENO, &rfds);
                struct timeval tv = {0, 10000};  /* 10 ms */
                int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
                if (r > 0) {
                    if (!fgets(input, INPUT_BUFFER_SIZE, stdin)) {
                        printf("\n");
                        goto repl_exit;
                    }
                    got_input = true;
                } else if (r == 0) {
                    /* Timeout — run one scheduler tick so background tasks make progress */
                    if (global_scheduler) {
                        scheduler_run_one_tick(global_scheduler);
                    }
                }
                /* r < 0: EINTR or other error — just retry */
            }
        }

        /* When not mid-form, check for exit commands and skip blank lines */
        if (accum_len == 0) {
            char trimmed[INPUT_BUFFER_SIZE];
            strncpy(trimmed, input, INPUT_BUFFER_SIZE - 1);
            trimmed[INPUT_BUFFER_SIZE - 1] = '\0';
            size_t tlen = strlen(trimmed);
            while (tlen > 0 && (trimmed[tlen-1] == '\n' || trimmed[tlen-1] == '\r'
                                 || trimmed[tlen-1] == ' ' || trimmed[tlen-1] == '\t'))
                trimmed[--tlen] = '\0';
            if (strcmp(trimmed, "(exit)") == 0 || strcmp(trimmed, "exit") == 0) break;
            if (tlen == 0) continue;
        }

        /* Append this line to the accumulation buffer (keep the newline so the
         * reader sees correct line structure and is_form_complete works). */
        size_t input_len = strlen(input);
        if (accum_len + input_len >= ACCUM_BUFFER_SIZE) {
            printf("Error: input too long (max %d bytes)\n", ACCUM_BUFFER_SIZE);
            accum_len = 0;
            accum[0] = '\0';
            continue;
        }
        memcpy(accum + accum_len, input, input_len);
        accum_len += input_len;
        accum[accum_len] = '\0';

        /* Wait until we have a syntactically complete top-level form */
        if (!is_form_complete(accum)) {
            continue;
        }

        /* Process the accumulated complete form(s) */
        Reader* reader = reader_new(accum, "<repl>");
        Value all_forms = reader_read_all(reader);

        if (reader_has_error(reader)) {
            printf("Read error: %s\n", reader_error_msg(reader));
            reader_free(reader);
            object_release(all_forms);
            accum_len = 0;
            accum[0] = '\0';
            line_number++;
            continue;
        }
        reader_free(reader);

        size_t n_forms = vector_length(all_forms);
        for (size_t fi = 0; fi < n_forms; fi++) {
            Value form = vector_get(all_forms, fi);

            Compiler* compiler = compiler_new("<repl>");
            CompiledCode* code = compile(compiler, form);

            if (compiler_has_error(compiler)) {
                printf("Compile error: %s\n", compiler_error_msg(compiler));
                compiled_code_free(code);
                compiler_free(compiler);
                continue;
            }

            int n_constants = (int)vector_length(code->constants);
            Value* constants = malloc(n_constants * sizeof(Value));
            for (int i = 0; i < n_constants; i++) {
                constants[i] = vector_get(code->constants, i);
            }
            for (int i = 0; i < n_constants; i++) {
                if (is_function(constants[i])) {
                    function_set_code(constants[i],
                                      code->bytecode, (int)code->code_size,
                                      constants, n_constants);
                    object_make_immortal(constants[i]);
                }
            }

            Value task_val = task_new_from_code(code->bytecode, (int)code->code_size,
                                                 constants, n_constants, global_scheduler);
            Task* repl_task = task_get(task_val);
            scheduler_run_task_to_completion(global_scheduler, repl_task);
            /* Do NOT call scheduler_run_until_done here — background tasks (server loops,
             * actors, etc.) should persist across REPL prompts and get CPU time during
             * scheduler_run_task_to_completion of the next evaluated form. */

            if (repl_task->vm->error) {
                printf("Runtime error: %s\n", repl_task->vm->error_msg);
            } else {
                if (!vm_stack_empty(repl_task->vm)) {
                    Value result = repl_task->vm->stack[repl_task->vm->stack_pointer - 1];
                    if (is_pointer(result)) object_retain(result);
                    vm_pop(repl_task->vm);
                    value_print_readable(result);
                    printf("\n");
                    if (is_pointer(result)) object_release(result);
                }
            }

            object_release(task_val);

            if (n_compiled_units < MAX_COMPILED_UNITS) {
                compiled_units[n_compiled_units] = code;
                constant_arrays[n_compiled_units] = constants;
                n_compiled_units++;
            } else {
                fprintf(stderr, "Warning: too many compiled units\n");
                compiled_code_free(code);
                free(constants);
            }

            compiler_free(compiler);
        }

        object_release(all_forms);

        /* Reset accumulator and advance prompt counter */
        accum_len = 0;
        accum[0] = '\0';
        line_number++;
    }
    repl_exit:;
    return 0;
}

/* ================================================================
 * Main
 * ================================================================ */

static void print_usage(void) {
    printf("Usage: beer [command] [options]\n\n");
    printf("Commands:\n");
    printf("  new <name>     Create a new project\n");
    printf("  run            Run the project's -main function\n");
    printf("  build          Build project into a .tar archive\n");
    printf("  ubertar        Build standalone .tar with dependencies\n");
    printf("  compile [dir]  Compile stale .beer files to .beerc\n");
    printf("  check   [dir]  Report stale / missing .beerc files\n");
    printf("  repl           Start a REPL (default)\n");
    printf("  <file.beer>    Run a script file\n");
    printf("\nOptions:\n");
    printf("  -e <expr>      Evaluate expression and print result (repeatable)\n");
    printf("  --trace        Enable opcode tracing\n");
    printf("  --help, -h     Show this help\n");
}

int main(int argc, char** argv) {
    bool trace = false;
    const char* subcommand = NULL;
    const char* subcmd_arg = NULL;
    const char* script_file = NULL;
    /* -e expressions: up to 64 */
    const char* eval_exprs[64];
    int n_eval_exprs = 0;

#ifdef BEER_TRACK_ALLOCS
    dump_leaks_flag = false;
#endif

    /* Parse flags and find subcommand */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) {
            trace = true;
#ifdef BEER_TRACK_ALLOCS
        } else if (strcmp(argv[i], "--dump-leaks") == 0) {
            dump_leaks_flag = true;
#endif
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "beer: -e requires an expression argument\n");
                return 1;
            }
            if (n_eval_exprs < 64) {
                eval_exprs[n_eval_exprs++] = argv[++i];
            }
        } else if (argv[i][0] != '-' && !subcommand) {
            subcommand = argv[i];
            /* Grab next arg if available (for 'new <name>') */
            if (i + 1 < argc && argv[i+1][0] != '-') {
                subcmd_arg = argv[i + 1];
            }
        }
    }

    /* Check if subcommand is actually a .beer file */
    if (subcommand) {
        size_t slen = strlen(subcommand);
        if (slen > 5 && strcmp(subcommand + slen - 5, ".beer") == 0) {
            script_file = subcommand;
            subcommand = NULL;
        }
    }

    if (subcommand && strcmp(subcommand, "new") == 0 && !subcmd_arg) {
        fprintf(stderr, "Usage: beer new <project-name>\n");
        return 1;
    }

    /* Initialize systems */
    log_init();
    if (trace) {
        log_set_level(ULOG_LEVEL_TRACE);
    }
    memory_init();
    symbol_init();
    namespace_init();

    int result = 0;

    if (n_eval_exprs > 0) {
        result = cmd_eval_exprs(eval_exprs, n_eval_exprs);
    } else if (script_file) {
        /* Script mode */
        Value path_str = string_from_cstr(script_file);
        Value load_argv[1] = { path_str };
        VM* vm = vm_new(256);
        vm->scheduler = global_scheduler;
        native_load(vm, 1, load_argv);
        result = vm->error ? 1 : 0;
        if (vm->error) {
            fprintf(stderr, "Error: %s\n", vm->error_msg);
        }
        if (global_scheduler) {
            scheduler_run_until_done(global_scheduler);
        }
        vm_free(vm);
        object_release(path_str);
    } else if (subcommand) {
        if (strcmp(subcommand, "new") == 0) {
            result = cmd_new(subcmd_arg);
        } else if (strcmp(subcommand, "run") == 0) {
            result = cmd_run();
        } else if (strcmp(subcommand, "build") == 0) {
            result = cmd_build();
        } else if (strcmp(subcommand, "ubertar") == 0) {
            result = cmd_ubertar();
        } else if (strcmp(subcommand, "compile") == 0) {
            result = cmd_compile(subcmd_arg);
        } else if (strcmp(subcommand, "check") == 0) {
            result = cmd_check(subcmd_arg);
        } else if (strcmp(subcommand, "repl") == 0) {
            /* Project REPL — load paths from beer.edn if present */
            ProjectPaths config;
            if (read_project_paths(&config)) {
                prepend_load_paths(&config);
            }
            result = run_repl();
        } else {
            fprintf(stderr, "Unknown command: %s\n", subcommand);
            print_usage();
            result = 1;
        }
    } else {
        /* Default: REPL, optionally with project paths */
        ProjectPaths config;
        if (read_project_paths(&config)) {
            prepend_load_paths(&config);
        }
        result = run_repl();
    }

    /* Cleanup */
    for (int i = 0; i < n_compiled_units; i++) {
        compiled_code_free(compiled_units[i]);
        free(constant_arrays[i]);
    }

#ifdef BEER_TRACK_ALLOCS
    maybe_dump_leaks();
#endif
    shutdown_systems();

    if (n_eval_exprs == 0 && !script_file && !subcommand) {
        printf("Goodbye!\n");
    } else if (subcommand && strcmp(subcommand, "repl") == 0) {
        printf("Goodbye!\n");
    }

    return result;
}
