/* Beerlang REPL - Main entry point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "beerlang.h"
#include "memory.h"
#include "symbol.h"
#include "namespace.h"
#include "reader.h"
#include "compiler.h"
#include "vm.h"
#include "value.h"
#include "function.h"
#include "scheduler.h"
#include "core.h"
#include "log.h"

#define INPUT_BUFFER_SIZE 4096
#define MAX_COMPILED_UNITS 1024

int main(int argc, char** argv) {
    bool trace = false;
#ifdef BEER_TRACK_ALLOCS
    bool dump_leaks = false;
#endif
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) {
            trace = true;
#ifdef BEER_TRACK_ALLOCS
        } else if (strcmp(argv[i], "--dump-leaks") == 0) {
            dump_leaks = true;
#endif
        }
    }

    /* Initialize systems */
    log_init();
    if (trace) {
        log_set_level(ULOG_LEVEL_TRACE);
    }
    memory_init();
    symbol_init();
    namespace_init();  /* This also registers all native functions */

    /*
     * Keep compiled code units alive for the lifetime of the REPL.
     * Function objects store pointers into their compilation unit's
     * bytecode and constants, so those must remain valid.
     */
    CompiledCode* compiled_units[MAX_COMPILED_UNITS];
    Value* constant_arrays[MAX_COMPILED_UNITS];  /* C arrays extracted from vectors */
    int n_compiled_units = 0;

    /* REPL loop */
    char input[INPUT_BUFFER_SIZE];
    /* Check for script file argument */
    const char* script_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            script_file = argv[i];
            break;
        }
    }

    if (script_file) {
        /* Script mode: load and execute the file, then exit */
        Value path_str = string_from_cstr(script_file);
        Value load_argv[1] = { path_str };
        VM* vm = vm_new(256);
        vm->scheduler = global_scheduler;
        native_load(vm, 1, load_argv);
        int had_error = vm->error;
        if (had_error) {
            fprintf(stderr, "Error: %s\n", vm->error_msg);
        }
        if (global_scheduler) {
            scheduler_run_until_done(global_scheduler);
        }
        vm_free(vm);
        object_release(path_str);

        namespace_shutdown();
        symbol_shutdown();
#ifdef BEER_TRACK_ALLOCS
        if (dump_leaks) {
            memory_dump_objects();
        }
#endif
        memory_shutdown();
        return had_error ? 1 : 0;
    }

    /* REPL mode: print banner */
    printf("Beerlang v%d.%d.%d\n",
           BEERLANG_VERSION_MAJOR,
           BEERLANG_VERSION_MINOR,
           BEERLANG_VERSION_PATCH);
    printf("Type (exit) to quit\n\n");

    int line_number = 1;

    while (true) {
        /* Print prompt with current namespace */
        Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
        printf("%s:%d> ", cur_ns ? cur_ns->name : "beerlang", line_number);
        fflush(stdout);

        /* Read input */
        if (!fgets(input, INPUT_BUFFER_SIZE, stdin)) {
            /* EOF or error */
            printf("\n");
            break;
        }

        /* Remove trailing newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        /* Skip empty lines */
        if (strlen(input) == 0) {
            continue;
        }

        /* Check for exit */
        if (strcmp(input, "(exit)") == 0 || strcmp(input, "exit") == 0) {
            break;
        }

        /* Read */
        Reader* reader = reader_new(input, "<repl>");
        Value form = reader_read(reader);

        if (reader_has_error(reader)) {
            printf("Read error: %s\n", reader_error_msg(reader));
            reader_free(reader);
            line_number++;
            continue;
        }

        /* Compile */
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

        /* Build constants C array from the compiled code's vector */
        int n_constants = (int)vector_length(code->constants);
        Value* constants = malloc(n_constants * sizeof(Value));
        for (int i = 0; i < n_constants; i++) {
            constants[i] = vector_get(code->constants, i);
        }

        /* Set execution context on any function objects in constants.
         * This makes functions self-contained: when called later from
         * a different VM, they carry their own bytecode and constants. */
        for (int i = 0; i < n_constants; i++) {
            if (is_function(constants[i])) {
                function_set_code(constants[i],
                                  code->bytecode, (int)code->code_size,
                                  constants, n_constants);
            }
        }

        /* Execute */
        VM* vm = vm_new(256);
        vm->scheduler = global_scheduler;
        vm_load_code(vm, code->bytecode, (int)code->code_size);
        vm_load_constants(vm, constants, n_constants);

        vm_run(vm);

        /* Drain scheduler — run any spawned tasks to completion */
        if (global_scheduler) {
            scheduler_run_until_done(global_scheduler);
        }

        if (vm->error) {
            printf("Runtime error: %s\n", vm->error_msg);
        } else {
            /* Print result if there is one */
            if (!vm_stack_empty(vm)) {
                /* Retain before pop: vm_pop releases, which may free the
                 * object if the stack is its only reference (e.g. a value
                 * loaded from a local variable in a synthetic function). */
                Value result = vm->stack[vm->stack_pointer - 1];
                if (is_pointer(result)) object_retain(result);
                vm_pop(vm);
                value_print_readable(result);
                printf("\n");
                if (is_pointer(result)) object_release(result);
            }
        }

        /* Clean up VM (fresh one each evaluation) */
        vm_free(vm);

        /* Keep compiled code and constants alive (functions reference them) */
        if (n_compiled_units < MAX_COMPILED_UNITS) {
            compiled_units[n_compiled_units] = code;
            constant_arrays[n_compiled_units] = constants;
            n_compiled_units++;
        } else {
            /* Overflow - free this unit (functions may break) */
            fprintf(stderr, "Warning: too many compiled units\n");
            compiled_code_free(code);
            free(constants);
        }

        compiler_free(compiler);
        reader_free(reader);
        object_release(form);

        line_number++;
    }

    /* Shutdown: free all kept compiled units */
    for (int i = 0; i < n_compiled_units; i++) {
        compiled_code_free(compiled_units[i]);
        free(constant_arrays[i]);
    }

    namespace_shutdown();
    symbol_shutdown();

#ifdef BEER_TRACK_ALLOCS
    if (dump_leaks) {
        memory_dump_objects();
    }
#endif
    memory_shutdown();

    printf("Goodbye!\n");
    return 0;
}
