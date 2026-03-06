/* Namespace and Var System
 *
 * Namespaces are collections of vars (global bindings).
 * This is a simplified single-threaded version.
 * Thread safety will be added in Phase 8 (concurrency).
 */

#ifndef BEERLANG_NAMESPACE_H
#define BEERLANG_NAMESPACE_H

#include "value.h"

/* Forward declarations */
typedef struct Var Var;
typedef struct Namespace Namespace;
typedef struct NamespaceRegistry NamespaceRegistry;

/* =================================================================
 * Var - A global binding (mutable container for a value)
 * ================================================================= */

struct Var {
    struct Object header;  /* Object header (type = TYPE_VAR) */
    Value name;      /* Symbol */
    Value value;     /* Current value */
    bool is_macro;   /* Is this a macro? */
    Value meta;      /* Metadata (map) */
};

/* Var API */
Var* var_new(Value name);
void var_free(Var* var);
Value var_get_value(Var* var);
void var_set_value(Var* var, Value value);

/* =================================================================
 * Namespace - A collection of vars
 * ================================================================= */

struct Namespace {
    struct Object header;    /* Object header (type = TYPE_NAMESPACE) */
    const char* name;        /* Namespace name (owned string) */
    Value vars;              /* HashMap: Symbol -> Var (as tagged pointer) */
    Value aliases;           /* HashMap: Symbol -> String (alias -> ns name) */
};

/* Namespace API */
Namespace* namespace_new(const char* name);
void namespace_free(Namespace* ns);

/* Define or update a var in namespace */
Var* namespace_define(Namespace* ns, Value symbol, Value value);

/* Lookup a var by symbol (returns NULL if not found) */
Var* namespace_lookup(Namespace* ns, Value symbol);

/* Remove a var from namespace */
void namespace_undefine(Namespace* ns, Value symbol);

/* Get all vars as a vector of vars */
Value namespace_all_vars(Namespace* ns);

/* Namespace alias management */
void namespace_add_alias(Namespace* ns, Value alias_sym, const char* target_ns_name);
const char* namespace_resolve_alias(Namespace* ns, Value alias_sym);

/* =================================================================
 * NamespaceRegistry - Global registry of all namespaces
 * ================================================================= */

struct NamespaceRegistry {
    Value namespaces;  /* HashMap: String -> Namespace* (as pointer value) */
    Namespace* current;  /* Current namespace (for REPL) */
};

/* Registry API */
NamespaceRegistry* namespace_registry_new(void);
void namespace_registry_free(NamespaceRegistry* reg);

/* Get or create namespace */
Namespace* namespace_registry_get_or_create(NamespaceRegistry* reg, const char* name);

/* Get namespace (returns NULL if not found) */
Namespace* namespace_registry_get(NamespaceRegistry* reg, const char* name);

/* Set current namespace */
void namespace_registry_set_current(NamespaceRegistry* reg, Namespace* ns);

/* Get current namespace */
Namespace* namespace_registry_current(NamespaceRegistry* reg);

/* Get beer.core namespace */
Namespace* namespace_registry_get_core(NamespaceRegistry* reg);

/* Global registry instance (initialized once) */
extern NamespaceRegistry* global_namespace_registry;

/* Initialize global registry (call once at startup) */
void namespace_init(void);

/* Shutdown global registry (call once at shutdown) */
void namespace_shutdown(void);

#endif /* BEERLANG_NAMESPACE_H */
