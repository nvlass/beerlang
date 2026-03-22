/* beer.shell namespace — shell/process execution */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include "vm.h"
#include "value.h"
#include "bstring.h"
#include "hashmap.h"
#include "symbol.h"
#include "namespace.h"
#include "memory.h"
#include "core.h"
#include "native.h"

/* (shell/exec cmd arg1 arg2 ...) => {:exit N :out "..." :err "..."} */
static Value native_shell_exec(VM* vm, int argc, Value* argv) {
    if (argc < 1) {
        vm_error(vm, "shell/exec: requires at least 1 string argument");
        return VALUE_NIL;
    }

    /* Validate all args are strings */
    for (int i = 0; i < argc; i++) {
        if (!is_string(argv[i])) {
            vm_error(vm, "shell/exec: all arguments must be strings");
            return VALUE_NIL;
        }
    }

    /* Build argv array for execvp */
    char** child_argv = malloc((argc + 1) * sizeof(char*));
    if (!child_argv) {
        vm_error(vm, "shell/exec: out of memory");
        return VALUE_NIL;
    }
    for (int i = 0; i < argc; i++) {
        child_argv[i] = (char*)string_cstr(argv[i]);
    }
    child_argv[argc] = NULL;

    /* Create pipes for stdout and stderr */
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        free(child_argv);
        vm_error(vm, "shell/exec: pipe() failed");
        return VALUE_NIL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        free(child_argv);
        vm_error(vm, "shell/exec: fork() failed");
        return VALUE_NIL;
    }

    if (pid == 0) {
        /* Child process */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execvp(child_argv[0], child_argv);
        /* If execvp returns, it failed */
        _exit(127);
    }

    /* Parent process */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    free(child_argv);

    /* Read stdout and stderr into growable buffers */
    size_t out_cap = 1024, out_len = 0;
    char* out_buf = malloc(out_cap);
    size_t err_cap = 1024, err_len = 0;
    char* err_buf = malloc(err_cap);

    if (!out_buf || !err_buf) {
        free(out_buf);
        free(err_buf);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        waitpid(pid, NULL, 0);
        vm_error(vm, "shell/exec: out of memory");
        return VALUE_NIL;
    }

    /* Read both fds (simple sequential read — fine for typical use) */
    ssize_t n;
    while ((n = read(stdout_pipe[0], out_buf + out_len, out_cap - out_len)) > 0) {
        out_len += n;
        if (out_len >= out_cap) {
            out_cap *= 2;
            out_buf = realloc(out_buf, out_cap);
        }
    }
    close(stdout_pipe[0]);

    while ((n = read(stderr_pipe[0], err_buf + err_len, err_cap - err_len)) > 0) {
        err_len += n;
        if (err_len >= err_cap) {
            err_cap *= 2;
            err_buf = realloc(err_buf, err_cap);
        }
    }
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    /* Build result map {:exit N :out "..." :err "..."} */
    Value result = hashmap_create_default();

    Value key_exit = keyword_intern("exit");
    Value val_exit = make_fixnum(exit_code);
    hashmap_set(result, key_exit, val_exit);

    Value key_out = keyword_intern("out");
    Value val_out = string_from_buffer(out_buf, out_len);
    hashmap_set(result, key_out, val_out);
    object_release(val_out);

    Value key_err = keyword_intern("err");
    Value val_err = string_from_buffer(err_buf, err_len);
    hashmap_set(result, key_err, val_err);
    object_release(val_err);

    free(out_buf);
    free(err_buf);

    return result;
}

/* Forward declaration — defined in core.c */
extern NamespaceRegistry* global_namespace_registry;

static void register_native_in(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
}

void core_register_shell(void) {
    Namespace* shell_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.shell");
    if (!shell_ns) return;
    register_native_in(shell_ns, "shell-exec", native_shell_exec);
}
