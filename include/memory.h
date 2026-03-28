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

/* Object allocation and deallocation */

/* Allocate a new object with given type and size
 * Size is the total size including the Object header.
 * Returns a raw pointer to the allocated object.
 * The object is initialized with refcount = 1.
 */
struct Object* object_alloc(uint8_t type, size_t size);

/* Increment reference count */
void object_retain(Value v);

/* Decrement reference count, free if it reaches zero */
void object_release(Value v);

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
