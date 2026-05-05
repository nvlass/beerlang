/* Namespace and Var System Implementation */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "namespace.h"
#include "value.h"
#include "memory.h"
#include "hashmap.h"
#include "symbol.h"
#include "vector.h"
#include "bstring.h"
#include "stream.h"
#include "task.h"
#include "channel.h"
#include "atom.h"
#include "scheduler.h"
#include "core.h"
#include "aot.h"

/* Global registry instance */
NamespaceRegistry* global_namespace_registry = NULL;

/* =================================================================
 * Var Implementation
 * ================================================================= */

/* Destructor called when Var refcount reaches zero */
static void var_destructor(struct Object* obj) {
    Var* var = (Var*)obj;

    /* Don't release var->name — it's an interned symbol whose lifetime
     * is managed by the intern table. The hashmap key also references it. */
    if (!is_nil(var->value)) {
        object_release(var->value);
    }
    if (!is_nil(var->meta)) {
        object_release(var->meta);
    }
}

Var* var_new(Value name) {
    Var* var = (Var*)object_alloc(TYPE_VAR, sizeof(Var));

    var->name = name;
    /* Don't retain — interned symbols are managed by the intern table */

    var->value = VALUE_NIL;
    var->is_macro = false;
    var->meta = VALUE_NIL;

    return var;
}

void var_free(Var* var) {
    if (!var) return;
    Value v = tag_pointer(var);
    object_release(v);
}

Value var_get_value(Var* var) {
    if (!var) return VALUE_NIL;
    return var->value;
}

void var_set_value(Var* var, Value value) {
    if (!var) return;

    /* Release old value */
    if (!is_nil(var->value)) {
        object_release(var->value);
    }

    /* Set new value */
    var->value = value;
    if (!is_nil(value)) {
        object_retain(value);
    }
}

/* =================================================================
 * Namespace Implementation
 * ================================================================= */

/* Destructor called when Namespace refcount reaches zero */
static void namespace_destructor(struct Object* obj) {
    Namespace* ns = (Namespace*)obj;

    if (!is_nil(ns->vars)) {
        object_release(ns->vars);
    }
    if (!is_nil(ns->aliases)) {
        object_release(ns->aliases);
    }

    free((void*)ns->name);
}

Namespace* namespace_new(const char* name) {
    Namespace* ns = (Namespace*)object_alloc(TYPE_NAMESPACE, sizeof(Namespace));

    ns->name = strdup(name);
    if (!ns->name) {
        /* Can't recover cleanly — fatal */
        fprintf(stderr, "FATAL: Out of memory in namespace_new\n");
        abort();
    }

    ns->vars = hashmap_create_default();
    ns->aliases = hashmap_create_default();
    return ns;
}

void namespace_free(Namespace* ns) {
    if (!ns) return;
    Value v = tag_pointer(ns);
    object_release(v);
}

Var* namespace_define(Namespace* ns, Value symbol, Value value) {
    if (!ns || !is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
        return NULL;
    }

    /* Check if var already exists */
    Value var_ptr_val = hashmap_get(ns->vars, symbol);
    Var* var;

    if (is_nil(var_ptr_val)) {
        /* Create new var */
        var = var_new(symbol);
        if (!var) return NULL;

        /* Store Var as tagged pointer in hashmap (ref-counted) */
        Value tagged_var = tag_pointer(var);
        hashmap_set(ns->vars, symbol, tagged_var);
        /* hashmap_set retains; var_new returned refcount=1; release our ref */
        object_release(tagged_var);
    } else {
        /* Update existing var */
        var = (Var*)untag_pointer(var_ptr_val);
    }

    /* Set the value */
    var_set_value(var, value);

    return var;
}

Var* namespace_lookup(Namespace* ns, Value symbol) {
    if (!ns || !is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
        return NULL;
    }

    Value var_ptr_val = hashmap_get(ns->vars, symbol);
    if (is_nil(var_ptr_val)) {
        return NULL;
    }

    return (Var*)untag_pointer(var_ptr_val);
}

void namespace_undefine(Namespace* ns, Value symbol) {
    if (!ns || !is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
        return;
    }

    /* hashmap_remove releases the Var value automatically */
    hashmap_remove(ns->vars, symbol);
}

Value namespace_all_vars(Namespace* ns) {
    if (!ns) return VALUE_NIL;

    /* Return vector of Var* values */
    Value result = vector_create(16);
    Value keys = hashmap_keys(ns->vars);
    size_t n = vector_length(keys);

    for (size_t i = 0; i < n; i++) {
        Value key = vector_get(keys, i);
        Value var_ptr_val = hashmap_get(ns->vars, key);

        if (!is_nil(var_ptr_val)) {
            vector_push(result, var_ptr_val);
        }
    }

    object_release(keys);
    return result;
}

/* Namespace alias management */
void namespace_add_alias(Namespace* ns, Value alias_sym, const char* target_ns_name) {
    if (!ns) return;
    Value name_str = string_from_cstr(target_ns_name);
    hashmap_set(ns->aliases, alias_sym, name_str);
    object_release(name_str);
}

const char* namespace_resolve_alias(Namespace* ns, Value alias_sym) {
    if (!ns) return NULL;
    Value name_str = hashmap_get(ns->aliases, alias_sym);
    if (is_nil(name_str)) return NULL;
    return string_cstr(name_str);
}

/* =================================================================
 * NamespaceRegistry Implementation
 * ================================================================= */

NamespaceRegistry* namespace_registry_new(void) {
    NamespaceRegistry* reg = (NamespaceRegistry*)malloc(sizeof(NamespaceRegistry));
    if (!reg) return NULL;

    reg->namespaces = hashmap_create_default();
    reg->current = NULL;

    return reg;
}

void namespace_registry_free(NamespaceRegistry* reg) {
    if (!reg) return;

    /* Release the retained current namespace reference */
    if (reg->current) {
        object_release(tag_pointer(reg->current));
        reg->current = NULL;
    }

    if (!is_nil(reg->namespaces)) {
        /* hashmap destructor releases all keys (strings) and values (Namespace objects) */
        object_release(reg->namespaces);
    }

    free(reg);
}

Namespace* namespace_registry_get_or_create(NamespaceRegistry* reg, const char* name) {
    if (!reg || !name) return NULL;

    /* Create string key */
    Value name_str = string_from_cstr(name);

    /* Check if namespace exists */
    Value ns_ptr_val = hashmap_get(reg->namespaces, name_str);
    Namespace* ns;

    if (is_nil(ns_ptr_val)) {
        /* Create new namespace */
        ns = namespace_new(name);
        if (!ns) {
            object_release(name_str);
            return NULL;
        }

        /* Store in registry */
        Value tagged_ns = tag_pointer(ns);
        hashmap_set(reg->namespaces, name_str, tagged_ns);
        /* hashmap_set retains; namespace_new returned refcount=1; release our ref */
        object_release(tagged_ns);
    } else {
        ns = (Namespace*)untag_pointer(ns_ptr_val);
    }

    object_release(name_str);
    return ns;
}

Namespace* namespace_registry_get(NamespaceRegistry* reg, const char* name) {
    if (!reg || !name) return NULL;

    Value name_str = string_from_cstr(name);
    Value ns_ptr_val = hashmap_get(reg->namespaces, name_str);
    object_release(name_str);

    if (is_nil(ns_ptr_val)) {
        return NULL;
    }

    return (Namespace*)untag_pointer(ns_ptr_val);
}

void namespace_registry_set_current(NamespaceRegistry* reg, Namespace* ns) {
    if (!reg) return;
    /* Retain new before releasing old (they might be the same) */
    if (ns) {
        object_retain(tag_pointer(ns));
    }
    if (reg->current) {
        object_release(tag_pointer(reg->current));
    }
    reg->current = ns;
}

Namespace* namespace_registry_current(NamespaceRegistry* reg) {
    if (!reg) return NULL;
    return reg->current;
}

Namespace* namespace_registry_get_core(NamespaceRegistry* reg) {
    return namespace_registry_get(reg, "beer.core");
}

/* =================================================================
 * Global Initialization
 * ================================================================= */

void namespace_init(void) {
    if (global_namespace_registry) {
        return;  /* Already initialized */
    }

    /* Initialize underlying systems */
    hashmap_init();
    vector_init();

    /* Register destructors for Var and Namespace types */
    object_register_destructor(TYPE_VAR, var_destructor);
    object_register_destructor(TYPE_NAMESPACE, namespace_destructor);

    global_namespace_registry = namespace_registry_new();

    /* Create beer.core namespace — all natives/macros go here */
    namespace_registry_get_or_create(global_namespace_registry, "beer.core");

    /* Create 'user' namespace and set as current */
    Namespace* user_ns = namespace_registry_get_or_create(global_namespace_registry, "user");
    namespace_registry_set_current(global_namespace_registry, user_ns);

    /* Register core native functions (into beer.core) */
    core_register_arithmetic();
    core_register_io();
    core_register_comparison();
    core_register_collections();
    core_register_predicates();
    core_register_utility();

    /* Initialize stream type and standard streams */
    stream_init();
    stream_init_standard();
    core_register_streams();

    /* Initialize task and channel types, register concurrency natives */
    task_init();
    channel_init();
    core_register_concurrency();

    /* Initialize atom type and register atom natives */
    atom_init();
    core_register_atoms();
    core_register_metadata();
    core_register_tar();
    core_register_shell();
    core_register_tcp();
    core_register_bits();
    core_register_crypto();
    core_register_aot();

    /* Initialize global scheduler */
    global_scheduler = scheduler_new(DEFAULT_TASK_QUOTA);

    /* Load core macros from lib/core.beer */
    core_register_macros();
}

void namespace_shutdown(void) {
    if (global_scheduler) {
        scheduler_free(global_scheduler);
        global_scheduler = NULL;
    }
    if (global_namespace_registry) {
        namespace_registry_free(global_namespace_registry);
        global_namespace_registry = NULL;
    }
}
