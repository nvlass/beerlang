/* Atom implementation - Mutable reference type */

#include <stdio.h>
#include <stdlib.h>
#include "atom.h"
#include "memory.h"
#include "vm.h"
#include "function.h"
#include "native.h"

/* Atom destructor */
static void atom_destroy(struct Object* obj) {
    Atom* atom = (Atom*)obj;
    if (is_pointer(atom->value)) {
        object_release(atom->value);
    }
    if (is_pointer(atom->watches)) {
        object_release(atom->watches);
    }
    if (is_pointer(atom->validator)) {
        object_release(atom->validator);
    }
}

/* Initialize atom type */
void atom_init(void) {
    object_register_destructor(TYPE_ATOM, atom_destroy);
}

/* Create a new atom */
Value atom_new(Value initial) {
    Atom* atom = (Atom*)object_alloc(TYPE_ATOM, sizeof(Atom));
    atom->value = initial;
    if (is_pointer(initial)) {
        object_retain(initial);
    }
    atom->watches = VALUE_NIL;
    atom->validator = VALUE_NIL;
    return tag_pointer(atom);
}

/* Get the current value */
Value atom_deref(Value atom_val) {
    Atom* atom = (Atom*)untag_pointer(atom_val);
    return atom->value;
}

/* Reset atom to new value, return new value */
Value atom_reset(Value atom_val, Value new_val) {
    Atom* atom = (Atom*)untag_pointer(atom_val);
    Value old = atom->value;
    if (is_pointer(new_val)) {
        object_retain(new_val);
    }
    atom->value = new_val;
    if (is_pointer(old)) {
        object_release(old);
    }
    return new_val;
}

/* Swap: apply fn to current value (+ extra args), store result */
Value atom_swap(VM* vm, Value atom_val, Value fn, int extra_argc, Value* extra_argv) {
    Atom* atom = (Atom*)untag_pointer(atom_val);
    int total_args = 1 + extra_argc;  /* current-val + extra args */

    if (is_pointer(fn) && object_type(fn) == TYPE_NATIVE_FN) {
        /* Native function: call directly */
        Value* args = malloc(sizeof(Value) * (size_t)total_args);
        args[0] = atom->value;
        for (int i = 0; i < extra_argc; i++) {
            args[i + 1] = extra_argv[i];
        }
        NativeFn native_fn = native_function_ptr(fn);
        Value result = native_fn(vm, total_args, args);
        free(args);

        /* Store result in atom */
        Value old = atom->value;
        atom->value = result;
        if (is_pointer(result)) {
            object_retain(result);
        }
        if (is_pointer(old)) {
            object_release(old);
        }
        return result;
    }

    /* Bytecode function: use temp VM pattern */
    int n_consts = total_args + 1;  /* args + function */
    Value* consts = malloc(sizeof(Value) * (size_t)n_consts);
    consts[0] = atom->value;  /* current value as first arg */
    for (int i = 0; i < extra_argc; i++) {
        consts[i + 1] = extra_argv[i];
    }
    consts[total_args] = fn;

    /* Bytecode: PUSH_CONST for each const, CALL total_args, HALT */
    size_t code_size = (size_t)(n_consts * 5 + 3 + 1);
    uint8_t* code = malloc(code_size);
    size_t pc = 0;

    for (int i = 0; i < n_consts; i++) {
        code[pc++] = OP_PUSH_CONST;
        code[pc++] = (uint8_t)(i & 0xFF);
        code[pc++] = (uint8_t)((i >> 8) & 0xFF);
        code[pc++] = (uint8_t)((i >> 16) & 0xFF);
        code[pc++] = (uint8_t)((i >> 24) & 0xFF);
    }

    code[pc++] = OP_CALL;
    code[pc++] = (uint8_t)(total_args & 0xFF);
    code[pc++] = (uint8_t)((total_args >> 8) & 0xFF);

    code[pc++] = OP_HALT;

    VM* temp_vm = vm_new(256);
    temp_vm->scheduler = vm->scheduler;
    vm_load_code(temp_vm, code, (int)pc);
    vm_load_constants(temp_vm, consts, n_consts);
    vm_run(temp_vm);

    Value result = VALUE_NIL;
    if (temp_vm->error) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "swap!: %s", temp_vm->error_msg);
        vm_error(vm, errbuf);
    } else if (temp_vm->stack_pointer > 0) {
        result = temp_vm->stack[temp_vm->stack_pointer - 1];
        if (is_pointer(result)) object_retain(result);
    }

    vm_free(temp_vm);
    free(code);
    free(consts);

    if (!vm->error) {
        /* Store result in atom */
        Value old = atom->value;
        atom->value = result;
        if (is_pointer(result)) {
            object_retain(result);
        }
        if (is_pointer(old)) {
            object_release(old);
        }
    }

    return result;
}

/* Compare and set */
bool atom_compare_and_set(Value atom_val, Value old_val, Value new_val) {
    Atom* atom = (Atom*)untag_pointer(atom_val);
    if (value_equal(atom->value, old_val)) {
        Value old = atom->value;
        if (is_pointer(new_val)) {
            object_retain(new_val);
        }
        atom->value = new_val;
        if (is_pointer(old)) {
            object_release(old);
        }
        return true;
    }
    return false;
}
