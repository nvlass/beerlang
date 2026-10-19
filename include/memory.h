/* Memory management - Allocator and reference counting
 *
 * All heap objects use reference counting for memory management.
 * Objects are automatically freed when their refcount reaches zero.
 */

#ifndef BEERLANG_MEMORY_H
#define BEERLANG_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "value.h"

/* Memory statistics */
typedef struct {
    size_t total_allocated;      /* Total bytes allocated */
    size_t total_freed;           /* Total bytes freed */
    size_t current_allocated;     /* Currently allocated bytes */
    size_t allocation_count;      /* Number of allocations */
    size_t free_count;            /* Number of frees */
    size_t objects_alive;         /* Number of live objects */
} MemoryStats;

/* Initialize memory subsystem */
void memory_init(void);

/* Shutdown memory subsystem and report leaks */
void memory_shutdown(void);

/* Get memory statistics */
MemoryStats memory_stats(void);

/* Print memory statistics (for debugging) */
void memory_print_stats(void);

#ifdef BEER_TRACK_ALLOCS
/* Dump all live objects to stderr (for leak debugging).
 * Only available when built with -DBEER_TRACK_ALLOCS (make track-leaks). */
void memory_dump_objects(void);
#endif

/* Immortal refcount — objects marked immortal are never freed.
 * Used for function template constants that live in compiled code's
 * constants array and may be shared across tasks/closures.
 * NOTE: A dedicated flag bit in the object header would be more robust
 * than a sentinel refcount value, but this is simpler for now. */
#define REFCOUNT_IMMORTAL UINT32_MAX

/* Mark an object as immortal (retain/release become no-ops) */
void object_make_immortal(Value v);

/* malloc/calloc/realloc that abort with a message instead of returning NULL */
void* xmalloc(size_t size);
void* xcalloc(size_t count, size_t size);
void* xrealloc(void* ptr, size_t size);

/* Object allocation and deallocation */

/* Allocate a new object with given type and size
 * Size is the total size including the Object header.
 * Returns a raw pointer to the allocated object.
 * The object is initialized with refcount = 1.
 */
struct Object* object_alloc(uint8_t type, size_t size);

/* Run the destructor and free an object whose refcount reached zero */
void object_destroy(struct Object* obj);

/* Increment reference count. Inline: this runs on every stack push. */
static inline void object_retain(Value v) {
    if (!is_pointer(v)) return;  /* Immediate values don't need refcounting */
    struct Object* obj = (struct Object*)untag_pointer(v);
    assert(obj != NULL);
    assert(obj->refcount > 0);   /* Object should be alive */
    if (obj->refcount != REFCOUNT_IMMORTAL) obj->refcount++;
}

/* Decrement reference count, free if it reaches zero */
static inline void object_release(Value v) {
    if (!is_pointer(v)) return;
    struct Object* obj = (struct Object*)untag_pointer(v);
    assert(obj != NULL);
    assert(obj->refcount > 0);
    if (obj->refcount == REFCOUNT_IMMORTAL) return;
    if (--obj->refcount == 0) object_destroy(obj);
}

/* Get current reference count (for debugging/testing) */
uint32_t object_refcount(Value v);

/* Type-specific deallocator function pointer
 * Called when an object's refcount reaches zero, before freeing memory.
 * Allows types to clean up internal resources (e.g., free bigint limbs).
 */
typedef void (*ObjectDestructor)(struct Object* obj);

/* Register a destructor for a specific object type */
void object_register_destructor(uint8_t type, ObjectDestructor destructor);

/* Direct memory allocation (for internal use by type implementations)
 * These bypass object tracking and should only be used for internal buffers.
 */
void* mem_alloc(size_t size);
void* mem_realloc(void* ptr, size_t old_size, size_t new_size);
void mem_free(void* ptr, size_t size);

#endif /* BEERLANG_MEMORY_H */
