/* Cons - Cons cells and list operations
 *
 * Cons cells are the fundamental building block of lists in Lisp.
 * A cons cell contains two values: car (first) and cdr (rest).
 *
 * Proper lists end with nil in the cdr.
 * Improper lists (dotted pairs) can have any value in the cdr.
 */

#ifndef BEERLANG_CONS_H
#define BEERLANG_CONS_H

#include <stdbool.h>
#include <stddef.h>
#include "value.h"

/* Initialize cons type (register destructor) */
void cons_init(void);

/* Cons cell object structure (heap-allocated)
 * Layout:
 *   Object header (16 bytes)
 *   Value car (first element)
 *   Value cdr (rest of list)
 */

/* Create a cons cell */
Value cons(Value car, Value cdr);

/* Access cons cell elements */
Value car(Value cons);
Value cdr(Value cons);

/* Setters (for mutable operations - use with caution) */
void set_car(Value cons, Value new_car);
void set_cdr(Value cons, Value new_cdr);

/* List construction */

/* Create empty list (nil) */
static inline Value list_empty(void) {
    return VALUE_NIL;
}

/* Create list from array of values */
Value list_from_array(const Value* values, size_t count);

/* Check if value is a cons cell */
bool is_cons(Value v);

/* Check if value is a proper list (ends with nil) */
bool is_proper_list(Value v);

/* Check if list is empty (nil) */
static inline bool is_empty_list(Value v) {
    return is_nil(v);
}

/* Get list length (only for proper lists, returns -1 for improper lists) */
int64_t list_length(Value list);

/* List access */

/* Get nth element (0-indexed), returns VALUE_NIL if out of bounds */
Value list_nth(Value list, size_t n);

/* Get first, second, third elements (convenience functions) */
static inline Value list_first(Value list) {
    return is_cons(list) ? car(list) : VALUE_NIL;
}

static inline Value list_second(Value list) {
    return is_cons(list) ? list_nth(list, 1) : VALUE_NIL;
}

static inline Value list_third(Value list) {
    return is_cons(list) ? list_nth(list, 2) : VALUE_NIL;
}

/* List operations */

/* Reverse a list (creates new list) */
Value list_reverse(Value list);

/* Append two lists (creates new list) */
Value list_append(Value list1, Value list2);

/* Map function over list elements (creates new list) */
typedef Value (*ListMapFn)(Value elem);
Value list_map(Value list, ListMapFn fn);

/* Filter list elements (creates new list) */
typedef bool (*ListFilterFn)(Value elem);
Value list_filter(Value list, ListFilterFn fn);

/* Fold/reduce list */
typedef Value (*ListFoldFn)(Value acc, Value elem);
Value list_fold(Value list, Value init, ListFoldFn fn);

/* Check if all elements satisfy predicate */
typedef bool (*ListPredicate)(Value elem);
bool list_every(Value list, ListPredicate pred);

/* Check if any element satisfies predicate */
bool list_any(Value list, ListPredicate pred);

/* List equality (deep comparison) */
bool list_equal(Value a, Value b);

/* Print list (for debugging) */
void cons_print(Value cons);
void list_print(Value list);
void list_print_readable(Value list);

#endif /* BEERLANG_CONS_H */
