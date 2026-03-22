/* Beerlang REPL - Main entry point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
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

#define INPUT_BUFFER_SIZE 4096
#define MAX_COMPILED_UNITS 1024

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
        }
    }

    VM* vm = vm_new(256);
    vm->scheduler = global_scheduler;
    vm_load_code(vm, code->bytecode, (int)code->code_size);
    vm_load_constants(vm, constants, n_constants);
    vm_run(vm);

    if (global_scheduler) {
        scheduler_run_until_done(global_scheduler);
    }

    int had_error = vm->error ? 1 : 0;
    if (vm->error) {
        fprintf(stderr, "Error: %s\n", vm->error_msg);
    }

    vm_free(vm);

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
 * beer.edn reading
 * ================================================================ */

typedef struct {
    char name[256];
    char version[64];
    char main_ns[256];
    char paths[16][256];
    int n_paths;
} ProjectConfig;

static bool read_beer_edn(ProjectConfig* config) {
    memset(config, 0, sizeof(ProjectConfig));

    FILE* f = fopen("beer.edn", "r");
    if (!f) return false;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = malloc(fsize + 1);
    fread(source, 1, fsize, f);
    source[fsize] = '\0';
    fclose(f);

    /* Parse with the reader */
    Reader* reader = reader_new(source, "beer.edn");
    Value form = reader_read(reader);
    if (reader_has_error(reader)) {
        fprintf(stderr, "Error reading beer.edn: %s\n", reader_error_msg(reader));
        reader_free(reader);
        free(source);
        return false;
    }
    reader_free(reader);
    free(source);

    if (!is_pointer(form) || object_type(form) != TYPE_HASHMAP) {
        fprintf(stderr, "beer.edn must be a map\n");
        object_release(form);
        return false;
    }

    /* Extract :name */
    Value k_name = keyword_intern("name");
    Value v_name = hashmap_get(form, k_name);
    if (is_string(v_name)) {
        snprintf(config->name, sizeof(config->name), "%s", string_cstr(v_name));
    }

    /* Extract :version */
    Value k_version = keyword_intern("version");
    Value v_version = hashmap_get(form, k_version);
    if (is_string(v_version)) {
        snprintf(config->version, sizeof(config->version), "%s", string_cstr(v_version));
    }

    /* Extract :main */
    Value k_main = keyword_intern("main");
    Value v_main = hashmap_get(form, k_main);
    if (is_pointer(v_main) && object_type(v_main) == TYPE_SYMBOL) {
        snprintf(config->main_ns, sizeof(config->main_ns), "%s", symbol_str(v_main));
    }

    /* Extract :paths */
    Value k_paths = keyword_intern("paths");
    Value v_paths = hashmap_get(form, k_paths);
    if (is_pointer(v_paths) && object_type(v_paths) == TYPE_VECTOR) {
        size_t n = vector_length(v_paths);
        if (n > 16) n = 16;
        for (size_t i = 0; i < n; i++) {
            Value p = vector_get(v_paths, i);
            if (is_string(p)) {
                snprintf(config->paths[config->n_paths], 256, "%s", string_cstr(p));
                config->n_paths++;
            }
        }
    }

    object_release(form);
    return true;
}

/* Prepend project paths to *load-path* */
static void prepend_load_paths(ProjectConfig* config) {
    Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
    if (!core_ns) return;

    Value lp_sym = symbol_intern("*load-path*");
    Var* lp_var = namespace_lookup(core_ns, lp_sym);
    if (!lp_var) return;

    Value old_lp = var_get_value(lp_var);
    Value new_lp = vector_create(config->n_paths + (int)vector_length(old_lp));

    /* Add project paths first */
    for (int i = 0; i < config->n_paths; i++) {
        char buf[512];
        size_t len = strlen(config->paths[i]);
        if (len > 0 && config->paths[i][len-1] == '/') {
            snprintf(buf, sizeof(buf), "%s", config->paths[i]);
        } else {
            snprintf(buf, sizeof(buf), "%s/", config->paths[i]);
        }
        Value s = string_from_cstr(buf);
        vector_push(new_lp, s);
        object_release(s);
    }

    /* Then existing paths */
    size_t n = vector_length(old_lp);
    for (size_t i = 0; i < n; i++) {
        vector_push(new_lp, vector_get(old_lp, i));
    }

    var_set_value(lp_var, new_lp);
    object_release(new_lp);
}

/* ================================================================
 * Subcommand: beer new <name>
 * ================================================================ */

static int mkdirp(const char* path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

static int cmd_new(const char* dir) {
    /* Extract project name from the last path component */
    const char* name = strrchr(dir, '/');
    name = name ? name + 1 : dir;

    /* Create directory structure */
    char path[1024];

    /* Convert dots to slashes for nested source dirs */
    char ns_path[256];
    snprintf(ns_path, sizeof(ns_path), "%s", name);
    for (char* p = ns_path; *p; p++) {
        if (*p == '.') *p = '/';
    }

    snprintf(path, sizeof(path), "%s/src/%s", dir, ns_path);
    mkdirp(path);
    snprintf(path, sizeof(path), "%s/lib", dir);
    mkdirp(path);
    snprintf(path, sizeof(path), "%s/test", dir);
    mkdirp(path);

    /* Write beer.edn */
    snprintf(path, sizeof(path), "%s/beer.edn", dir);
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot create %s: %s\n", path, strerror(errno));
        return 1;
    }
    fprintf(f, "{:name \"%s\"\n :version \"0.1.0\"\n :paths [\"src\" \"lib\"]\n :dependencies []\n :main %s.core}\n", name, name);
    fclose(f);

    /* Write src/<name>/core.beer */
    snprintf(path, sizeof(path), "%s/src/%s/core.beer", dir, ns_path);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot create %s: %s\n", path, strerror(errno));
        return 1;
    }
    fprintf(f, "(ns %s.core)\n\n(defn -main []\n  (println \"Hello from %s!\"))\n", name, name);
    fclose(f);

    printf("Created project: %s\n", name);
    printf("  %s/\n", dir);
    printf("  ├── beer.edn\n");
    printf("  ├── src/%s/core.beer\n", ns_path);
    printf("  ├── lib/\n");
    printf("  └── test/\n");
    return 0;
}

/* ================================================================
 * Subcommand: beer run
 * ================================================================ */

static int cmd_run(void) {
    ProjectConfig config;
    if (!read_beer_edn(&config)) {
        fprintf(stderr, "Error: beer.edn not found in current directory\n");
        return 1;
    }
    if (config.main_ns[0] == '\0') {
        fprintf(stderr, "Error: :main not specified in beer.edn\n");
        return 1;
    }

    prepend_load_paths(&config);

    /* Build expression: (do (require 'main.ns) (main.ns/-main)) */
    char expr[1024];
    snprintf(expr, sizeof(expr),
             "(do (require '%s) (%s/-main))", config.main_ns, config.main_ns);

    return eval_form(expr, "<beer run>");
}

/* ================================================================
 * Subcommand: beer build / beer ubertar
 * ================================================================ */

static int cmd_build(void) {
    ProjectConfig config;
    if (!read_beer_edn(&config)) {
        fprintf(stderr, "Error: beer.edn not found in current directory\n");
        return 1;
    }

    prepend_load_paths(&config);

    /* Delegate to beer.tools/build */
    char expr[2048];
    snprintf(expr, sizeof(expr),
             "(do (require 'beer.tools) (beer.tools/build))");
    return eval_form(expr, "<beer build>");
}

static int cmd_ubertar(void) {
    ProjectConfig config;
    if (!read_beer_edn(&config)) {
        fprintf(stderr, "Error: beer.edn not found in current directory\n");
        return 1;
    }

    prepend_load_paths(&config);

    char expr[2048];
    snprintf(expr, sizeof(expr),
             "(do (require 'beer.tools) (beer.tools/ubertar))");
    return eval_form(expr, "<beer ubertar>");
}

/* ================================================================
 * REPL mode
 * ================================================================ */

static int run_repl(void) {
    char input[INPUT_BUFFER_SIZE];

    printf("Beerlang v%d.%d.%d\n",
           BEERLANG_VERSION_MAJOR,
           BEERLANG_VERSION_MINOR,
           BEERLANG_VERSION_PATCH);
    printf("Type (exit) to quit\n\n");

    int line_number = 1;

    while (true) {
        Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
        printf("%s:%d> ", cur_ns ? cur_ns->name : "beerlang", line_number);
        fflush(stdout);

        if (!fgets(input, INPUT_BUFFER_SIZE, stdin)) {
            printf("\n");
            break;
        }

        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        if (strlen(input) == 0) continue;

        if (strcmp(input, "(exit)") == 0 || strcmp(input, "exit") == 0) break;

        Reader* reader = reader_new(input, "<repl>");
        Value form = reader_read(reader);

        if (reader_has_error(reader)) {
            printf("Read error: %s\n", reader_error_msg(reader));
            reader_free(reader);
            line_number++;
            continue;
        }

        Compiler* compiler = compiler_new("<repl>");
        CompiledCode* code = compile(compiler, form);

        if (compiler_has_error(compiler)) {
            printf("Compile error: %s\n", compiler_error_msg(compiler));
            compiled_code_free(code);
            compiler_free(compiler);
            reader_free(reader);
            object_release(form);
            line_number++;
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
            }
        }

        VM* vm = vm_new(256);
        vm->scheduler = global_scheduler;
        vm_load_code(vm, code->bytecode, (int)code->code_size);
        vm_load_constants(vm, constants, n_constants);

        vm_run(vm);

        if (global_scheduler) {
            scheduler_run_until_done(global_scheduler);
        }

        if (vm->error) {
            printf("Runtime error: %s\n", vm->error_msg);
        } else {
            if (!vm_stack_empty(vm)) {
                Value result = vm->stack[vm->stack_pointer - 1];
                if (is_pointer(result)) object_retain(result);
                vm_pop(vm);
                value_print_readable(result);
                printf("\n");
                if (is_pointer(result)) object_release(result);
            }
        }

        vm_free(vm);

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
        reader_free(reader);
        object_release(form);

        line_number++;
    }

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
    printf("  repl           Start a REPL (default)\n");
    printf("  <file.beer>    Run a script file\n");
    printf("\nOptions:\n");
    printf("  --trace        Enable opcode tracing\n");
    printf("  --help, -h     Show this help\n");
}

int main(int argc, char** argv) {
    bool trace = false;
    const char* subcommand = NULL;
    const char* subcmd_arg = NULL;
    const char* script_file = NULL;

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

    /* 'new' doesn't need full runtime init */
    if (subcommand && strcmp(subcommand, "new") == 0) {
        if (!subcmd_arg) {
            fprintf(stderr, "Usage: beer new <project-name>\n");
            return 1;
        }
        return cmd_new(subcmd_arg);
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

    if (script_file) {
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
        /* Try to load beer.edn for project-aware commands */
        if (strcmp(subcommand, "run") == 0) {
            result = cmd_run();
        } else if (strcmp(subcommand, "build") == 0) {
            result = cmd_build();
        } else if (strcmp(subcommand, "ubertar") == 0) {
            result = cmd_ubertar();
        } else if (strcmp(subcommand, "repl") == 0) {
            /* Project REPL — load paths from beer.edn if present */
            ProjectConfig config;
            if (read_beer_edn(&config)) {
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
        ProjectConfig config;
        if (read_beer_edn(&config)) {
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

    if (!script_file && !subcommand) {
        printf("Goodbye!\n");
    } else if (subcommand && strcmp(subcommand, "repl") == 0) {
        printf("Goodbye!\n");
    }

    return result;
}
