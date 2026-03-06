/* Cons cell and list operations implementation */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "cons.h"
#include "beerlang.h"

/* Cons cell object layout */
typedef struct {
    struct Object header;
    Value car;  /* First element */
    Value cdr;  /* Rest of list */
} Cons;

/* Destructor - release car and cdr */
static void cons_destructor(struct Object* obj) {
    Cons* c = (Cons*)obj;

    /* Release car if it's a heap object */
    if (is_pointer(c->car)) {
        object_release(c->car);
    }

    /* Release cdr if it's a heap object */
    if (is_pointer(c->cdr)) {
        object_release(c->cdr);
    }
}

/* Initialize cons type (idempotent - safe to call multiple times) */
static void cons_init_type(void) {
    object_register_destructor(TYPE_CONS, cons_destructor);
}

/* Public initialization function (call at startup) */
void cons_init(void) {
    cons_init_type();
}

/* Create a cons cell */
Value cons(Value car_val, Value cdr_val) {
    cons_init_type();

    Cons* c = (Cons*)object_alloc(TYPE_CONS, sizeof(Cons));
    Value obj = tag_pointer(c);

    /* Store car and cdr */
    c->car = car_val;
    c->cdr = cdr_val;

    /* Retain if heap objects */
    if (is_pointer(car_val)) {
        object_retain(car_val);
    }
    if (is_pointer(cdr_val)) {
        object_retain(cdr_val);
    }

    return obj;
}

/* Access car */
Value car(Value cons_val) {
    assert(is_pointer(cons_val) && object_type(cons_val) == TYPE_CONS);
    Cons* c = (Cons*)untag_pointer(cons_val);
    return c->car;
}

/* Access cdr */
Value cdr(Value cons_val) {
    assert(is_pointer(cons_val) && object_type(cons_val) == TYPE_CONS);
    Cons* c = (Cons*)untag_pointer(cons_val);
    return c->cdr;
}

/* Set car (mutable operation) */
void set_car(Value cons_val, Value new_car) {
    assert(is_pointer(cons_val) && object_type(cons_val) == TYPE_CONS);
    Cons* c = (Cons*)untag_pointer(cons_val);

    /* Retain new value */
    if (is_pointer(new_car)) {
        object_retain(new_car);
    }

    /* Release old value */
    if (is_pointer(c->car)) {
        object_release(c->car);
    }

    c->car = new_car;
}

/* Set cdr (mutable operation) */
void set_cdr(Value cons_val, Value new_cdr) {
    assert(is_pointer(cons_val) && object_type(cons_val) == TYPE_CONS);
    Cons* c = (Cons*)untag_pointer(cons_val);

    /* Retain new value */
    if (is_pointer(new_cdr)) {
        object_retain(new_cdr);
    }

    /* Release old value */
    if (is_pointer(c->cdr)) {
        object_release(c->cdr);
    }

    c->cdr = new_cdr;
}

/* Create list from array */
Value list_from_array(const Value* values, size_t count) {
    if (count == 0) {
        return VALUE_NIL;
    }

    /* Build list from right to left */
    Value result = VALUE_NIL;
    for (size_t i = count; i > 0; i--) {
        Value new_cons = cons(values[i - 1], result);
        if (is_pointer(result)) {
            object_release(result);
        }
        result = new_cons;
    }

    return result;
}

/* Check if value is a cons cell */
bool is_cons(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_CONS;
}

/* Check if value is a proper list */
bool is_proper_list(Value v) {
    if (is_nil(v)) {
        return true;  /* Empty list is proper */
    }

    if (!is_cons(v)) {
        return false;  /* Not a list at all */
    }

    /* Walk the list, checking for nil termination or cycles */
    Value slow = v;
    Value fast = v;

    while (true) {
        /* Check fast pointer */
        if (is_nil(fast)) {
            return true;  /* Proper termination */
        }

        if (!is_cons(fast)) {
            return false;  /* Improper list (dotted pair) */
        }

        fast = cdr(fast);

        if (is_nil(fast)) {
            return true;  /* Proper termination */
        }

        if (!is_cons(fast)) {
            return false;  /* Improper list */
        }

        fast = cdr(fast);
        slow = cdr(slow);

        /* Cycle detection (Floyd's algorithm) */
        if (value_identical(fast, slow)) {
            return false;  /* Circular list */
        }
    }
}

/* Get list length */
int64_t list_length(Value list) {
    if (is_nil(list)) {
        return 0;
    }

    if (!is_cons(list)) {
        return -1;  /* Not a list */
    }

    int64_t length = 0;
    Value slow = list;
    Value fast = list;

    while (is_cons(fast)) {
        length++;
        fast = cdr(fast);

        if (is_nil(fast)) {
            return length;  /* Proper list */
        }

        if (!is_cons(fast)) {
            return -1;  /* Improper list */
        }

        length++;
        fast = cdr(fast);
        slow = cdr(slow);

        /* Cycle detection */
        if (value_identical(fast, slow) && length > 0) {
            return -1;  /* Circular list */
        }
    }

    if (is_nil(fast)) {
        return length;
    }

    return -1;  /* Improper list */
}

/* Get nth element */
Value list_nth(Value list, size_t n) {
    Value current = list;

    for (size_t i = 0; i < n; i++) {
        if (!is_cons(current)) {
            return VALUE_NIL;  /* Out of bounds */
        }
        current = cdr(current);
    }

    if (is_cons(current)) {
        return car(current);
    }

    return VALUE_NIL;  /* Out of bounds */
}

/* Reverse a list */
Value list_reverse(Value list) {
    Value result = VALUE_NIL;
    Value current = list;

    while (is_cons(current)) {
        Value new_cons = cons(car(current), result);
        /* Release old result since we're replacing it */
        if (is_pointer(result)) {
            object_release(result);
        }
        result = new_cons;
        current = cdr(current);
    }

    return result;
}

/* Append two lists */
Value list_append(Value list1, Value list2) {
    if (is_nil(list1)) {
        /* Retain list2 since we're returning it */
        if (is_pointer(list2)) {
            object_retain(list2);
        }
        return list2;
    }

    /* Build reversed list1 */
    Value reversed = VALUE_NIL;
    Value current = list1;

    while (is_cons(current)) {
        Value new_cons = cons(car(current), reversed);
        if (is_pointer(reversed)) {
            object_release(reversed);
        }
        reversed = new_cons;
        current = cdr(current);
    }

    /* Build result by reversing reversed list1 and consing onto list2 */
    Value result = list2;
    /* Retain list2 since it becomes part of our result */
    if (is_pointer(list2)) {
        object_retain(list2);
    }

    current = reversed;
    while (is_cons(current)) {
        Value new_cons = cons(car(current), result);
        if (is_pointer(result)) {
            object_release(result);
        }
        result = new_cons;
        current = cdr(current);
    }

    /* Release the reversed temporary list */
    if (is_pointer(reversed)) {
        object_release(reversed);
    }

    return result;
}

/* Map function over list */
Value list_map(Value list, ListMapFn fn) {
    if (is_nil(list)) {
        return VALUE_NIL;
    }

    if (!is_cons(list)) {
        return VALUE_NIL;  /* Not a list */
    }

    /* Build result list (reversed) */
    Value result = VALUE_NIL;
    Value current = list;

    while (is_cons(current)) {
        Value mapped = fn(car(current));
        Value new_cons = cons(mapped, result);
        if (is_pointer(result)) {
            object_release(result);
        }
        result = new_cons;
        current = cdr(current);
    }

    /* Reverse to get correct order */
    Value reversed = list_reverse(result);
    if (is_pointer(result)) {
        object_release(result);
    }
    return reversed;
}

/* Filter list */
Value list_filter(Value list, ListFilterFn fn) {
    if (is_nil(list)) {
        return VALUE_NIL;
    }

    if (!is_cons(list)) {
        return VALUE_NIL;  /* Not a list */
    }

    /* Build result list (reversed) */
    Value result = VALUE_NIL;
    Value current = list;

    while (is_cons(current)) {
        Value elem = car(current);
        if (fn(elem)) {
            Value new_cons = cons(elem, result);
            if (is_pointer(result)) {
                object_release(result);
            }
            result = new_cons;
        }
        current = cdr(current);
    }

    /* Reverse to get correct order */
    Value reversed = list_reverse(result);
    if (is_pointer(result)) {
        object_release(result);
    }
    return reversed;
}

/* Fold/reduce list */
Value list_fold(Value list, Value init, ListFoldFn fn) {
    Value acc = init;
    Value current = list;

    while (is_cons(current)) {
        acc = fn(acc, car(current));
        current = cdr(current);
    }

    return acc;
}

/* Check if all elements satisfy predicate */
bool list_every(Value list, ListPredicate pred) {
    Value current = list;

    while (is_cons(current)) {
        if (!pred(car(current))) {
            return false;
        }
        current = cdr(current);
    }

    return true;  /* Empty list or all elements satisfy predicate */
}

/* Check if any element satisfies predicate */
bool list_any(Value list, ListPredicate pred) {
    Value current = list;

    while (is_cons(current)) {
        if (pred(car(current))) {
            return true;
        }
        current = cdr(current);
    }

    return false;  /* Empty list or no elements satisfy predicate */
}

/* List equality (deep comparison) */
bool list_equal(Value a, Value b) {
    /* Fast path: same pointer */
    if (value_identical(a, b)) {
        return true;
    }

    /* Both must be lists */
    if (is_nil(a) && is_nil(b)) {
        return true;
    }

    if (!is_cons(a) || !is_cons(b)) {
        return false;
    }

    /* Compare elements */
    Value curr_a = a;
    Value curr_b = b;

    while (is_cons(curr_a) && is_cons(curr_b)) {
        /* Compare cars using value_equal */
        if (!value_equal(car(curr_a), car(curr_b))) {
            return false;
        }

        curr_a = cdr(curr_a);
        curr_b = cdr(curr_b);
    }

    /* Both should end with nil for proper lists */
    return value_equal(curr_a, curr_b);
}

/* Print cons cell */
void cons_print(Value cons_val) {
    assert(is_pointer(cons_val) && object_type(cons_val) == TYPE_CONS);

    printf("(");
    value_print(car(cons_val));
    printf(" . ");
    value_print(cdr(cons_val));
    printf(")");
}

/* Print list */
void list_print(Value list) {
    if (is_nil(list)) {
        printf("()");
        return;
    }

    if (!is_cons(list)) {
        value_print(list);  /* Not a list, print as value */
        return;
    }

    printf("(");
    Value current = list;
    bool first = true;

    while (is_cons(current)) {
        if (!first) {
            printf(" ");
        }
        first = false;

        value_print(car(current));
        current = cdr(current);
    }

    /* Handle improper list */
    if (!is_nil(current)) {
        printf(" . ");
        value_print(current);
    }

    printf(")");
}

void list_print_readable(Value list) {
    if (is_nil(list)) {
        printf("()");
        return;
    }

    if (!is_cons(list)) {
        value_print_readable(list);
        return;
    }

    printf("(");
    Value current = list;
    bool first = true;

    while (is_cons(current)) {
        if (!first) {
            printf(" ");
        }
        first = false;

        value_print_readable(car(current));
        current = cdr(current);
    }

    if (!is_nil(current)) {
        printf(" . ");
        value_print_readable(current);
    }

    printf(")");
}
