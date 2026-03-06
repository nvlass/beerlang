/* Memory allocator and reference counting implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "memory.h"
#include "beerlang.h"
#ifdef BEER_TRACK_ALLOCS
#include "native.h"
#endif

/* Global memory statistics */
static MemoryStats g_stats = {0};

#ifdef BEER_TRACK_ALLOCS
/* Intrusive linked list of all live objects */
static struct Object* g_all_objects = NULL;
#endif

/* Destructor table - indexed by object type */
#define MAX_OBJECT_TYPES 256
static ObjectDestructor g_destructors[MAX_OBJECT_TYPES] = {NULL};

/* Initialize memory subsystem */
void memory_init(void) {
    memset(&g_stats, 0, sizeof(MemoryStats));
#ifdef BEER_TRACK_ALLOCS
    g_all_objects = NULL;
#endif
    /* Don't clear g_destructors - they are type-level metadata that persists across tests */
}

/* Shutdown and report leaks */
void memory_shutdown(void) {
    if (g_stats.current_allocated > 0 || g_stats.objects_alive > 0) {
        fprintf(stderr, "WARNING: Memory leaks detected!\n");
        fprintf(stderr, "  Bytes still allocated: %zu\n", g_stats.current_allocated);
        fprintf(stderr, "  Objects still alive: %zu\n", g_stats.objects_alive);
    }
}

/* Get memory statistics */
MemoryStats memory_stats(void) {
    return g_stats;
}

/* Print memory statistics */
void memory_print_stats(void) {
    printf("Memory Statistics:\n");
    printf("  Total allocated: %zu bytes\n", g_stats.total_allocated);
    printf("  Total freed: %zu bytes\n", g_stats.total_freed);
    printf("  Current allocated: %zu bytes\n", g_stats.current_allocated);
    printf("  Allocations: %zu\n", g_stats.allocation_count);
    printf("  Frees: %zu\n", g_stats.free_count);
    printf("  Live objects: %zu\n", g_stats.objects_alive);
}

/* Allocate a new object */
struct Object* object_alloc(uint8_t type, size_t size) {
    /* Ensure size is at least as large as Object header */
    if (size < sizeof(struct Object)) {
        size = sizeof(struct Object);
    }

    /* Round up to multiple of 8 for consistent sizing */
    size_t aligned_size = (size + 7) & ~7;

    /* calloc returns zeroed, sufficiently aligned memory */
    struct Object* obj = calloc(1, aligned_size);
    if (!obj) {
        fprintf(stderr, "FATAL: Out of memory (requested %zu bytes)\n", aligned_size);
        abort();
    }

    /* Initialize object header */
    obj->type = type;
    obj->refcount = 1;  /* Start with refcount = 1 */
    obj->size = (uint32_t)size;  /* Store original size, not aligned */
    obj->meta = NULL;

#ifdef BEER_TRACK_ALLOCS
    /* Add to live object list */
    obj->next_alloc = g_all_objects;
    g_all_objects = obj;
#endif

    /* Update statistics with aligned size (actual allocation) */
    g_stats.total_allocated += aligned_size;
    g_stats.current_allocated += aligned_size;
    g_stats.allocation_count++;
    g_stats.objects_alive++;

    return obj;
}

/* Increment reference count */
void object_retain(Value v) {
    if (!is_pointer(v)) {
        return;  /* Immediate values don't need refcounting */
    }

    struct Object* obj = (struct Object*)untag_pointer(v);
    assert(obj != NULL);
    assert(obj->refcount > 0);  /* Object should be alive */

    obj->refcount++;
}

/* Decrement reference count and free if zero */
void object_release(Value v) {
    if (!is_pointer(v)) {
        return;  /* Immediate values don't need refcounting */
    }

    struct Object* obj = (struct Object*)untag_pointer(v);
    assert(obj != NULL);
    assert(obj->refcount > 0);  /* Object should be alive */

    obj->refcount--;

    if (obj->refcount == 0) {
        /* Call type-specific destructor if registered */
        uint8_t type = obj->type & 0xFF;
        if (g_destructors[type]) {
            g_destructors[type](obj);
        }

#ifdef BEER_TRACK_ALLOCS
        /* Remove from live object list */
        if (g_all_objects == obj) {
            g_all_objects = obj->next_alloc;
        } else {
            struct Object* prev = g_all_objects;
            while (prev && prev->next_alloc != obj) {
                prev = prev->next_alloc;
            }
            if (prev) {
                prev->next_alloc = obj->next_alloc;
            }
        }
#endif

        /* Update statistics (use aligned size to match allocation) */
        size_t size = obj->size;
        size_t aligned_size = (size + 7) & ~7;
        g_stats.total_freed += aligned_size;
        g_stats.current_allocated -= aligned_size;
        g_stats.free_count++;
        g_stats.objects_alive--;

        /* Free the object */
        free(obj);
    }
}

/* Get current reference count */
uint32_t object_refcount(Value v) {
    if (!is_pointer(v)) {
        return 0;  /* Immediate values have no refcount */
    }

    struct Object* obj = (struct Object*)untag_pointer(v);
    assert(obj != NULL);
    return obj->refcount;
}

/* Register a destructor for an object type */
void object_register_destructor(uint8_t type, ObjectDestructor destructor) {
    g_destructors[type] = destructor;
}

#ifdef BEER_TRACK_ALLOCS
/* Dump all live objects to stderr (for leak debugging) */
void memory_dump_objects(void) {
    if (!g_all_objects) {
        fprintf(stderr, "No live objects.\n");
        return;
    }

    fprintf(stderr, "\n=== Live Objects (%zu) ===\n", g_stats.objects_alive);

    /* Count by type */
    int counts[MAX_OBJECT_TYPES] = {0};
    struct Object* obj = g_all_objects;
    while (obj) {
        counts[obj->type & 0xFF]++;
        obj = obj->next_alloc;
    }

    /* Print summary by type */
    const char* type_names[] = {
        [TYPE_BIGINT]    = "bigint",
        [TYPE_FLOAT]     = "float",
        [TYPE_STRING]    = "string",
        [TYPE_SYMBOL]    = "symbol",
        [TYPE_KEYWORD]   = "keyword",
        [TYPE_CONS]      = "cons",
        [TYPE_VECTOR]    = "vector",
        [TYPE_HASHMAP]   = "hashmap",
        [TYPE_FUNCTION]  = "function",
        [TYPE_NATIVE_FN] = "native-fn",
        [TYPE_VAR]       = "var",
        [TYPE_NAMESPACE] = "namespace",
        [TYPE_HAMT_NODE] = "hamt-node",
        [TYPE_HAMT_COLLISION] = "hamt-collision",
    };

    fprintf(stderr, "\nBy type:\n");
    for (int i = 0; i < MAX_OBJECT_TYPES; i++) {
        if (counts[i] > 0) {
            const char* name = (i < (int)(sizeof(type_names)/sizeof(type_names[0])) && type_names[i])
                               ? type_names[i] : "unknown";
            fprintf(stderr, "  %-12s %d\n", name, counts[i]);
        }
    }

    /* Print each object with detail */
    fprintf(stderr, "\nDetails:\n");
    obj = g_all_objects;
    int shown = 0;
    while (obj && shown < 200) {
        Value v = tag_pointer(obj);
        uint8_t type = obj->type & 0xFF;
        const char* tname = (type < (int)(sizeof(type_names)/sizeof(type_names[0])) && type_names[type])
                            ? type_names[type] : "unknown";
        fprintf(stderr, "  [rc=%u] %-12s ", obj->refcount, tname);

        /* Print a short representation */
        switch (type) {
            case TYPE_STRING:
                fprintf(stderr, "\"%s\"", string_cstr(v));
                break;
            case TYPE_SYMBOL:
                fprintf(stderr, "%s", symbol_name(v));
                break;
            case TYPE_KEYWORD:
                fprintf(stderr, ":%s", keyword_name(v));
                break;
            case TYPE_FUNCTION:
                fprintf(stderr, "#<fn %s>", function_name(v));
                break;
            case TYPE_NATIVE_FN:
                fprintf(stderr, "#<fn %s>", native_function_name(v));
                break;
            case TYPE_VAR:
                fprintf(stderr, "#<var>");
                break;
            case TYPE_NAMESPACE:
                fprintf(stderr, "#<namespace>");
                break;
            default:
                fprintf(stderr, "@%p", (void*)obj);
                break;
        }
        fprintf(stderr, "\n");

        obj = obj->next_alloc;
        shown++;
    }

    if (obj) {
        fprintf(stderr, "  ... (truncated, more objects exist)\n");
    }

    fprintf(stderr, "=========================\n\n");
}
#endif /* BEER_TRACK_ALLOCS */

/* Direct memory allocation (for internal use) */
void* mem_alloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "FATAL: Out of memory (requested %zu bytes)\n", size);
        abort();
    }
    return ptr;
}

void* mem_realloc(void* ptr, size_t old_size, size_t new_size) {
    (void)old_size;  /* Unused, but kept for API consistency */
    void* new_ptr = realloc(ptr, new_size);
    if (!new_ptr && new_size > 0) {
        fprintf(stderr, "FATAL: Out of memory (requested %zu bytes)\n", new_size);
        abort();
    }
    return new_ptr;
}

void mem_free(void* ptr, size_t size) {
    (void)size;  /* Unused, but kept for API consistency */
    free(ptr);
}
