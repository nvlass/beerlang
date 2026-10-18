/* Atom implementation - Mutable reference type */

#include <stdio.h>
#include <stdlib.h>
#include "atom.h"
#include "memory.h"
#include "vm.h"
#include "function.h"
#include "native.h"
#include "scheduler.h"

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
