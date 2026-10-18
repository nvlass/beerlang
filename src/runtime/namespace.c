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
#include "strbuilder.h"
#include "scheduler.h"
#include "core.h"
#include "aot.h"
#include "cffi.h"

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
    var->is_dynamic = false;
    var->meta = VALUE_NIL;

    return var;
}

void var_free(Var* var) {
    if (!var) return;
    Value v = tag_pointer(var);
    object_release(v);
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
    if (!is_nil(ns->refers)) {
        object_release(ns->refers);
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
    ns->refers = hashmap_create_default();
    return ns;
}

void namespace_free(Namespace* ns) {
    if (!ns) return;
    Value v = tag_pointer(ns);
    object_release(v);
}

/* Bumped whenever symbol resolution could change (a var, refer or alias
 * appears or disappears) so the VM's LOAD_VAR cache knows to re-resolve.
 * Starts at 1 so zeroed cache entries never match. */
uint32_t namespace_generation = 1;

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
        namespace_generation++;
    } else {
        /* Update existing var */
        var = (Var*)untag_pointer(var_ptr_val);
    }

    /* Set the value */
    var_set_value(var, value);

    return var;
}

/* ---- Dynamic bindings ---- */

static Task* binding_task(void) {
    return global_scheduler ? global_scheduler->current : NULL;
}

/* Marks "not bound" in hashmap_get_default (a binding may be nil) */
static Value unbound_marker(void) {
    static Value marker;
    static bool made = false;
    if (!made) { marker = keyword_intern("beer.core/unbound"); made = true; }
    return marker;
}

static bool lookup_binding(Task* t, Var* var, Value* out) {
    if (!t || !is_pointer(t->bindings)) return false;
    Value v = hashmap_get_default(t->bindings, var_binding_key(var), unbound_marker());
    if (is_pointer(v) && v.as.object == unbound_marker().as.object) return false;
    *out = v;
    return true;
}

Value var_dynamic_value(Var* var) {
    Value v;
    if (lookup_binding(binding_task(), var, &v)) return v;
    return var->value;
}

bool var_is_bound(Var* var) {
    Value v;
    return lookup_binding(binding_task(), var, &v);
}

void var_set_binding(Var* var, Value v) {
    Task* t = binding_task();
    if (!t || !is_pointer(t->bindings)) return;
    Value updated = hashmap_assoc(t->bindings, var_binding_key(var), v);
    object_release(t->bindings);
    t->bindings = updated;
}

Var* namespace_ns_var(void) {
    static Var* ns_var = NULL;
    if (!ns_var && global_namespace_registry) {
        Namespace* core = namespace_registry_get_core(global_namespace_registry);
        if (core) ns_var = namespace_lookup(core, symbol_intern("*ns*"));
    }
    return ns_var;
}

Namespace* namespace_registry_current(NamespaceRegistry* reg) {
    if (!reg) return NULL;
    Task* t = binding_task();
    if (t && is_pointer(t->bindings)) {
        Var* nv = namespace_ns_var();
        Value sym;
        if (nv && lookup_binding(t, nv, &sym) && is_pointer(sym) && object_type(sym) == TYPE_SYMBOL) {
            if (!t->ns_cache || t->ns_cache_sym.as.object != sym.as.object) {
                t->ns_cache = namespace_registry_get_or_create(reg, symbol_str(sym));
                t->ns_cache_sym = sym;  /* interned: no retain needed */
            }
            if (t->ns_cache) return t->ns_cache;
        }
    }
    return reg->current;
}

Var* namespace_lookup(Namespace* ns, Value symbol) {
    if (!ns || !is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
        return NULL;
    }

    Value var_ptr_val = hashmap_get(ns->vars, symbol);
    if (is_nil(var_ptr_val)) {
        var_ptr_val = hashmap_get(ns->refers, symbol);
        if (is_nil(var_ptr_val)) return NULL;
    }

    return (Var*)untag_pointer(var_ptr_val);
}

void namespace_refer(Namespace* ns, Value symbol, Var* var) {
    if (!ns || !var) return;
    hashmap_set(ns->refers, symbol, tag_pointer(var));
    namespace_generation++;
}

void namespace_undefine(Namespace* ns, Value symbol) {
    if (!ns || !is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
        return;
    }

    /* hashmap_remove releases the Var value automatically */
    hashmap_remove(ns->vars, symbol);
    namespace_generation++;
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
    namespace_generation++;
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
    reg->core = NULL;

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

Namespace* namespace_registry_get_core(NamespaceRegistry* reg) {
    if (!reg) return NULL;
    if (!reg->core) reg->core = namespace_registry_get(reg, "beer.core");
    return reg->core;
}

/* =================================================================
 * Global Initialization
 * ================================================================= */

static void mark_core_dynamic_vars(void);

void namespace_init_bare(void) {
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
    core_register_string();

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
    strbuilder_init();
    core_register_atoms();
    core_register_metadata();
    core_register_tar();
    core_register_bytes();
    /* Optional features (beerlang.mk: BEER_SHELL, BEER_NET, ...) */
#ifndef BEER_NO_SHELL
    core_register_shell();
#endif
#ifndef BEER_NO_NET
    core_register_tcp();
    core_register_udp();
#endif
    core_register_bits();
#ifndef BEER_NO_CRYPTO
    core_register_crypto();
#endif
    core_register_json();
#ifndef BEER_NO_AOT
    core_register_aot();
#endif

#ifdef BEER_CFFI
    cpointer_init();
    core_register_ffi();
#endif

    /* Initialize global scheduler */
    global_scheduler = scheduler_new(DEFAULT_TASK_QUOTA);
    mark_core_dynamic_vars();
}

/* Vars that binding can give a task-local value (REPL sessions, loads) */
static void mark_core_dynamic_vars(void) {
    Namespace* core = namespace_registry_get_core(global_namespace_registry);
    const char* names[] = { "*ns*", "*out*", "*in*", "*err*", "*load-path*" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        Var* var = namespace_lookup(core, symbol_intern(names[i]));
        if (var) var->is_dynamic = true;
    }
}

void namespace_init(void) {
    namespace_init_bare();
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
    namespace_generation++;
}
