/* Tests for core native functions: collections, predicates, utilities */

#include "test.h"
#include "beerlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * eval_string helper — compile and run a source string, return result.
 * Calls memory_init/symbol_init/namespace_init; caller must call
 * eval_cleanup() when done with the result.
 * ================================================================ */

/* Kept-alive compiled units so function pointers stay valid */
#define MAX_UNITS 64
static CompiledCode* kept_code[MAX_UNITS];
static Value*        kept_consts[MAX_UNITS];
static int           n_kept = 0;

static void eval_init(void) {
    memory_init();
    symbol_init();
    namespace_init();
    n_kept = 0;
}

static void eval_cleanup(void) {
    for (int i = 0; i < n_kept; i++) {
        compiled_code_free(kept_code[i]);
        free(kept_consts[i]);
    }
    n_kept = 0;
    namespace_shutdown();
    symbol_shutdown();
    memory_shutdown();
}

/* Evaluate a single form string. Returns the result Value.
 * Sets *err to true if a read/compile/runtime error occurred. */
static Value eval_one(const char* source, bool* err) {
    *err = false;

    Reader* r = reader_new(source, "<test>");
    Value form = reader_read(r);
    if (reader_has_error(r)) {
        fprintf(stderr, "Read error: %s\n", reader_error_msg(r));
        reader_free(r);
        *err = true;
        return VALUE_NIL;
    }
    reader_free(r);

    Compiler* c = compiler_new("<test>");
    CompiledCode* code = compile(c, form);
    if (compiler_has_error(c) || !code) {
        fprintf(stderr, "Compile error: %s\n", compiler_error_msg(c));
        if (code) compiled_code_free(code);
        compiler_free(c);
        if (!is_nil(form)) object_release(form);
        *err = true;
        return VALUE_NIL;
    }
    compiler_free(c);
    if (!is_nil(form)) object_release(form);

    /* Build constants array */
    int n_consts = (int)vector_length(code->constants);
    Value* const_arr = malloc(sizeof(Value) * (n_consts > 0 ? n_consts : 1));
    for (int i = 0; i < n_consts; i++) {
        const_arr[i] = vector_get(code->constants, i);
    }

    /* Set code context on function constants (like REPL does) */
    for (int i = 0; i < n_consts; i++) {
        if (is_function(const_arr[i])) {
            function_set_code(const_arr[i],
                              code->bytecode, (int)code->code_size,
                              const_arr, n_consts);
        }
    }

    /* Execute */
    VM* vm = vm_new(256);
    vm_load_code(vm, code->bytecode, (int)code->code_size);
    vm_load_constants(vm, const_arr, n_consts);
    vm_run(vm);

    Value result = VALUE_NIL;
    if (vm->error) {
        *err = true;
    } else if (!vm_stack_empty(vm)) {
        result = vm->stack[vm->stack_pointer - 1];
        /* Retain heap values so they survive vm_free */
        if (is_pointer(result)) object_retain(result);
        vm_pop(vm);
    }

    vm_free(vm);

    /* Keep compiled unit alive */
    if (n_kept < MAX_UNITS) {
        kept_code[n_kept] = code;
        kept_consts[n_kept] = const_arr;
        n_kept++;
    } else {
        compiled_code_free(code);
        free(const_arr);
    }

    return result;
}

/* Evaluate multiple semicolon-delimited expressions in sequence.
 * Each expression is compiled and executed independently against
 * the same namespace (like the REPL). Returns last result. */
static Value eval_multi(const char* exprs[], int count, bool* err) {
    Value result = VALUE_NIL;
    for (int i = 0; i < count; i++) {
        result = eval_one(exprs[i], err);
        if (*err) return VALUE_NIL;
    }
    return result;
}

/* Convenience: single expression eval */
static Value eval_string(const char* source, bool* err) {
    return eval_one(source, err);
}

/* Release a retained result if it's a heap pointer */
static void release_result(Value v) {
    if (is_pointer(v) && !is_nil(v))
        object_release(v);
}

/* ================================================================
 * Collection Tests
 * ================================================================ */

TEST(list_creation) {
    eval_init();
    bool err;
    Value r = eval_string("(list 1 2 3)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_cons(r), "Should be a cons (list)");
    ASSERT(list_length(r) == 3, "Should have 3 elements");
    ASSERT(is_fixnum(car(r)) && untag_fixnum(car(r)) == 1, "First should be 1");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(list_empty) {
    eval_init();
    bool err;
    Value r = eval_string("(list)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_nil(r), "(list) should be nil");
    eval_cleanup();
    return NULL;
}

TEST(vector_creation) {
    eval_init();
    bool err;
    Value r = eval_string("(vector 1 2 3)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_vector(r), "Should be a vector");
    ASSERT(vector_length(r) == 3, "Should have 3 elements");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(hash_map_creation) {
    eval_init();
    bool err;
    Value r = eval_string("(hash-map :a 1 :b 2)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_hashmap(r), "Should be a hashmap");
    ASSERT(hashmap_size(r) == 2, "Should have 2 entries");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(hash_map_odd_args) {
    eval_init();
    bool err;
    eval_string("(hash-map :a 1 :b)", &err);
    ASSERT(err, "hash-map with odd args should error");
    eval_cleanup();
    return NULL;
}

TEST(cons_to_list) {
    eval_init();
    bool err;
    Value r = eval_string("(cons 0 (list 1 2))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_cons(r), "Should be a cons");
    ASSERT(untag_fixnum(car(r)) == 0, "First should be 0");
    ASSERT(list_length(r) == 3, "Should have 3 elements");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(cons_to_nil) {
    eval_init();
    bool err;
    Value r = eval_string("(cons 1 nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_cons(r), "Should be a cons");
    ASSERT(list_length(r) == 1, "Should have 1 element");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(first_list) {
    eval_init();
    bool err;
    Value r = eval_string("(first (list 10 20))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 10, "first should be 10");
    eval_cleanup();
    return NULL;
}

TEST(first_vector) {
    eval_init();
    bool err;
    Value r = eval_string("(first (vector 10 20))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 10, "first should be 10");
    eval_cleanup();
    return NULL;
}

TEST(first_nil) {
    eval_init();
    bool err;
    Value r = eval_string("(first nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_nil(r), "first of nil should be nil");
    eval_cleanup();
    return NULL;
}

TEST(rest_list) {
    eval_init();
    bool err;
    Value r = eval_string("(count (rest (list 1 2 3)))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 2, "rest of 3-list should have count 2");
    eval_cleanup();
    return NULL;
}

TEST(rest_nil) {
    eval_init();
    bool err;
    Value r = eval_string("(rest nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_nil(r), "rest of nil should be nil");
    eval_cleanup();
    return NULL;
}

TEST(nth_list) {
    eval_init();
    bool err;
    Value r = eval_string("(nth (list 10 20 30) 1)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 20, "nth 1 should be 20");
    eval_cleanup();
    return NULL;
}

TEST(nth_vector) {
    eval_init();
    bool err;
    Value r = eval_string("(nth (vector 10 20 30) 2)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 30, "nth 2 should be 30");
    eval_cleanup();
    return NULL;
}

TEST(count_list) {
    eval_init();
    bool err;
    Value r = eval_string("(count (list 1 2 3))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 3, "count should be 3");
    eval_cleanup();
    return NULL;
}

TEST(count_vector) {
    eval_init();
    bool err;
    Value r = eval_string("(count (vector 1 2))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 2, "count should be 2");
    eval_cleanup();
    return NULL;
}

TEST(count_nil) {
    eval_init();
    bool err;
    Value r = eval_string("(count nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 0, "count of nil should be 0");
    eval_cleanup();
    return NULL;
}

TEST(count_map) {
    eval_init();
    bool err;
    Value r = eval_string("(count (hash-map :a 1))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 1, "count of 1-entry map should be 1");
    eval_cleanup();
    return NULL;
}

TEST(conj_list) {
    eval_init();
    bool err;
    Value r = eval_string("(first (conj (list 2 3) 1))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 1, "conj to list prepends; first should be 1");
    eval_cleanup();
    return NULL;
}

TEST(conj_vector) {
    eval_init();
    bool err;
    Value r = eval_string("(count (conj (vector 1 2) 3))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 3, "conj to vector appends; count should be 3");
    eval_cleanup();
    return NULL;
}

TEST(empty_nil) {
    eval_init();
    bool err;
    Value r = eval_string("(empty? nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "nil should be empty");
    eval_cleanup();
    return NULL;
}

TEST(empty_list) {
    eval_init();
    bool err;
    Value r = eval_string("(empty? (list 1))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "non-empty list should not be empty");
    eval_cleanup();
    return NULL;
}

TEST(empty_vector) {
    eval_init();
    bool err;
    Value r = eval_string("(empty? (vector))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "empty vector should be empty");
    eval_cleanup();
    return NULL;
}

TEST(get_map) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1 :b 2))",
        "(get m :a)"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 1, "get :a should return 1");
    eval_cleanup();
    return NULL;
}

TEST(get_default) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1))",
        "(get m :z 99)"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 99, "get missing key should return default 99");
    eval_cleanup();
    return NULL;
}

TEST(assoc_map) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1))",
        "(count (assoc m :b 2))"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 2, "assoc should produce map with 2 entries");
    eval_cleanup();
    return NULL;
}

TEST(dissoc_map) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1 :b 2))",
        "(count (dissoc m :a))"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 1, "dissoc should produce map with 1 entry");
    eval_cleanup();
    return NULL;
}

TEST(keys_vals) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1 :b 2))",
        "(count (keys m))"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 2, "keys should have 2 elements");
    eval_cleanup();
    return NULL;
}

TEST(vals_count) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1 :b 2))",
        "(count (vals m))"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 2, "vals should have 2 elements");
    eval_cleanup();
    return NULL;
}

TEST(contains_map) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1 :b 2))",
        "(contains? m :a)"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "contains? :a should be true");
    eval_cleanup();
    return NULL;
}

TEST(contains_map_missing) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def m (hash-map :a 1))",
        "(contains? m :z)"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "contains? :z should be false");
    eval_cleanup();
    return NULL;
}

/* ================================================================
 * Predicate Tests
 * ================================================================ */

TEST(nil_pred_true) {
    eval_init();
    bool err;
    Value r = eval_string("(nil? nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "nil? of nil should be true");
    eval_cleanup();
    return NULL;
}

TEST(nil_pred_false) {
    eval_init();
    bool err;
    Value r = eval_string("(nil? 0)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "nil? of 0 should be false");
    eval_cleanup();
    return NULL;
}

TEST(number_pred_true) {
    eval_init();
    bool err;
    Value r = eval_string("(number? 42)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "number? of 42 should be true");
    eval_cleanup();
    return NULL;
}

TEST(number_pred_false) {
    eval_init();
    bool err;
    Value r = eval_string("(number? \"x\")", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "number? of string should be false");
    eval_cleanup();
    return NULL;
}

TEST(string_pred_true) {
    eval_init();
    bool err;
    Value r = eval_string("(string? \"hi\")", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "string? of string should be true");
    eval_cleanup();
    return NULL;
}

TEST(string_pred_false) {
    eval_init();
    bool err;
    Value r = eval_string("(string? 42)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "string? of number should be false");
    eval_cleanup();
    return NULL;
}

TEST(list_pred_true) {
    eval_init();
    bool err;
    Value r = eval_string("(list? (list 1))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "list? of list should be true");
    eval_cleanup();
    return NULL;
}

TEST(list_pred_false) {
    eval_init();
    bool err;
    Value r = eval_string("(list? (vector 1))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "list? of vector should be false");
    eval_cleanup();
    return NULL;
}

TEST(vector_pred) {
    eval_init();
    bool err;
    Value r = eval_string("(vector? (vector 1 2))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "vector? of vector should be true");
    eval_cleanup();
    return NULL;
}

TEST(map_pred) {
    eval_init();
    bool err;
    Value r = eval_string("(map? (hash-map :a 1))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "map? of hashmap should be true");
    eval_cleanup();
    return NULL;
}

TEST(fn_pred_native) {
    eval_init();
    bool err;
    Value r = eval_string("(fn? +)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "fn? of + should be true");
    eval_cleanup();
    return NULL;
}

TEST(fn_pred_false) {
    eval_init();
    bool err;
    Value r = eval_string("(fn? 42)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "fn? of number should be false");
    eval_cleanup();
    return NULL;
}

/* ================================================================
 * Utility Function Tests
 * ================================================================ */

TEST(not_false) {
    eval_init();
    bool err;
    Value r = eval_string("(not false)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "(not false) should be true");
    eval_cleanup();
    return NULL;
}

TEST(not_nil) {
    eval_init();
    bool err;
    Value r = eval_string("(not nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_true(r), "(not nil) should be true");
    eval_cleanup();
    return NULL;
}

TEST(not_truthy) {
    eval_init();
    bool err;
    Value r = eval_string("(not 42)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_false(r), "(not 42) should be false");
    eval_cleanup();
    return NULL;
}

TEST(str_concat) {
    eval_init();
    bool err;
    Value r = eval_string("(str \"a\" \"b\")", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_pointer(r) && object_type(r) == TYPE_STRING, "str should return a string");
    ASSERT_STR_EQ(string_cstr(r), "ab", "str should concatenate");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(str_mixed) {
    eval_init();
    bool err;
    Value r = eval_string("(str \"x\" 42)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_pointer(r) && object_type(r) == TYPE_STRING, "str should return a string");
    ASSERT_STR_EQ(string_cstr(r), "x42", "str should stringify numbers");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(type_fixnum) {
    eval_init();
    bool err;
    Value r = eval_string("(type 42)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_pointer(r) && object_type(r) == TYPE_KEYWORD, "type should return a keyword");
    ASSERT_STR_EQ(keyword_name(r), "fixnum", "type of 42 should be :fixnum");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(type_string) {
    eval_init();
    bool err;
    Value r = eval_string("(type \"hi\")", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_pointer(r) && object_type(r) == TYPE_KEYWORD, "type should return a keyword");
    ASSERT_STR_EQ(keyword_name(r), "string", "type of string should be :string");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(type_nil) {
    eval_init();
    bool err;
    Value r = eval_string("(type nil)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_pointer(r) && object_type(r) == TYPE_KEYWORD, "type should return a keyword");
    ASSERT_STR_EQ(keyword_name(r), "nil", "type of nil should be :nil");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(apply_native) {
    eval_init();
    bool err;
    Value r = eval_string("(apply + (list 1 2 3))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 6, "apply + (list 1 2 3) should be 6");
    eval_cleanup();
    return NULL;
}

TEST(apply_with_fixed_args) {
    eval_init();
    bool err;
    Value r = eval_string("(apply + 1 2 (list 3))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 6, "apply + 1 2 (list 3) should be 6");
    eval_cleanup();
    return NULL;
}

/* ================================================================
 * Closure Tests
 * ================================================================ */

TEST(basic_closure) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def adder (fn [x] (fn [y] (+ x y))))",
        "((adder 10) 5)"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 15, "adder(10)(5) should be 15");
    eval_cleanup();
    return NULL;
}

TEST(closure_over_let) {
    eval_init();
    bool err;
    Value r = eval_string("((let* [x 10] (fn [y] (+ x y))) 5)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 15, "let closure should return 15");
    eval_cleanup();
    return NULL;
}

TEST(closure_multiple_vars) {
    eval_init();
    bool err;
    Value r = eval_string("((fn [a b] ((fn [c] (+ a (+ b c))) 3)) 10 20)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 33, "Should capture two vars: 10+20+3=33");
    eval_cleanup();
    return NULL;
}

TEST(nested_closure) {
    eval_init();
    bool err;
    Value r = eval_string("((((fn [x] (fn [y] (fn [z] (+ x (+ y z))))) 100) 20) 3)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 123, "3-level closure: 100+20+3=123");
    eval_cleanup();
    return NULL;
}

TEST(closure_preserves_value) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def make-adder (fn [x] (fn [y] (+ x y))))",
        "(def add5 (make-adder 5))",
        "(def add10 (make-adder 10))",
        "(+ (add5 100) (add10 100))"
    };
    Value r = eval_multi(exprs, 4, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 215, "add5(100)+add10(100)=105+110=215");
    eval_cleanup();
    return NULL;
}

TEST(closure_with_cond) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def make-cmp (fn [threshold] (fn [x] (if (> x threshold) \"big\" \"small\"))))",
        "((make-cmp 10) 15)"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_pointer(r) && object_type(r) == TYPE_STRING, "Should return a string");
    ASSERT_STR_EQ(string_cstr(r), "big", "15 > 10 should be big");
    release_result(r);
    eval_cleanup();
    return NULL;
}

TEST(closure_inline) {
    eval_init();
    bool err;
    Value r = eval_string("((fn [x] ((fn [y] (+ x y)) 5)) 10)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 15, "Inline closure: 10+5=15");
    eval_cleanup();
    return NULL;
}

TEST(closure_self_contained) {
    eval_init();
    bool err;
    const char* exprs[] = {
        "(def make-counter (fn [start] (fn [n] (+ start n))))",
        "(def c1 (make-counter 0))",
        "(def c2 (make-counter 100))",
        "(+ (c1 5) (c2 5))"
    };
    Value r = eval_multi(exprs, 4, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 110, "c1(5)+c2(5)=5+105=110");
    eval_cleanup();
    return NULL;
}

/* ================================================================
 * Loop/Recur Tests
 * ================================================================ */

TEST(loop_basic_countdown) {
    eval_init();
    bool err;
    /* Count down from 5, return 0 */
    Value r = eval_string("(loop [i 5] (if (= i 0) i (recur (+ i -1))))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 0, "countdown should end at 0");
    eval_cleanup();
    return NULL;
}

TEST(loop_accumulator) {
    eval_init();
    bool err;
    /* Factorial of 10 via loop */
    Value r = eval_string("(loop [i 1 acc 1] (if (> i 10) acc (recur (+ i 1) (* acc i))))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 3628800, "10! should be 3628800");
    eval_cleanup();
    return NULL;
}

TEST(loop_nested_if) {
    eval_init();
    bool err;
    /* Sum numbers where i < 3, returning early */
    Value r = eval_string(
        "(loop [i 0 acc 0]"
        "  (if (= i 5) acc"
        "    (if (< i 3)"
        "      (recur (+ i 1) (+ acc i))"
        "      (recur (+ i 1) acc))))", &err);
    ASSERT(!err, "Should eval without error");
    /* 0 + 1 + 2 = 3 (only first 3 iterations add to acc) */
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 3, "sum of i<3 should be 3");
    eval_cleanup();
    return NULL;
}

TEST(loop_multiple_bindings) {
    eval_init();
    bool err;
    /* Fibonacci: fib(10) = 55 */
    Value r = eval_string("(loop [n 10 a 0 b 1] (if (= n 0) a (recur (+ n -1) b (+ a b))))", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 55, "fib(10) should be 55");
    eval_cleanup();
    return NULL;
}

TEST(recur_in_fn) {
    eval_init();
    bool err;
    /* recur targeting fn params (no loop) */
    Value r = eval_string("((fn fact [n acc] (if (< n 2) acc (recur (+ n -1) (* acc n)))) 10 1)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 3628800, "fact(10) via recur should be 3628800");
    eval_cleanup();
    return NULL;
}

TEST(recur_wrong_arity) {
    eval_init();
    bool err;
    /* recur with wrong number of args should be a compile error */
    eval_string("(loop [i 0] (recur 1 2))", &err);
    ASSERT(err, "recur with wrong arity should error");
    eval_cleanup();
    return NULL;
}

TEST(recur_not_tail) {
    eval_init();
    bool err;
    /* recur not in tail position should be a compile error */
    eval_string("(loop [i 0] (+ 1 (recur i)))", &err);
    ASSERT(err, "recur not in tail position should error");
    eval_cleanup();
    return NULL;
}

TEST(recur_outside_loop) {
    eval_init();
    bool err;
    /* recur outside loop/fn should be a compile error */
    eval_string("(recur 1)", &err);
    ASSERT(err, "recur outside loop/fn should error");
    eval_cleanup();
    return NULL;
}

TEST(loop_inside_fn) {
    eval_init();
    bool err;
    /* def a function that uses loop internally */
    const char* exprs[] = {
        "(def fact (fn [n] (loop [i n acc 1] (if (< i 2) acc (recur (+ i -1) (* acc i))))))",
        "(fact 10)"
    };
    Value r = eval_multi(exprs, 2, &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 3628800, "fact(10) via loop should be 3628800");
    eval_cleanup();
    return NULL;
}

TEST(loop_let_inside) {
    eval_init();
    bool err;
    /* let bindings inside loop body */
    Value r = eval_string(
        "(loop [i 0 acc 0]"
        "  (if (= i 5) acc"
        "    (let* [x (* i i)]"
        "      (recur (+ i 1) (+ acc x)))))", &err);
    ASSERT(!err, "Should eval without error");
    /* 0^2 + 1^2 + 2^2 + 3^2 + 4^2 = 0 + 1 + 4 + 9 + 16 = 30 */
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 30, "sum of squares 0-4 should be 30");
    eval_cleanup();
    return NULL;
}

TEST(let_inside_fn_fix) {
    eval_init();
    bool err;
    /* Regression test for ENTER patching fix */
    Value r = eval_string("((fn [a] (let* [x 1] (+ a x))) 10)", &err);
    ASSERT(!err, "Should eval without error");
    ASSERT(is_fixnum(r) && untag_fixnum(r) == 11, "fn(10) with let [x 1] should be 11");
    eval_cleanup();
    return NULL;
}

/* ================================================================
 * Test Runner
 * ================================================================ */

static const char* all_tests(void) {
    /* Collections */
    RUN_TEST(list_creation);
    RUN_TEST(list_empty);
    RUN_TEST(vector_creation);
    RUN_TEST(hash_map_creation);
    RUN_TEST(hash_map_odd_args);
    RUN_TEST(cons_to_list);
    RUN_TEST(cons_to_nil);
    RUN_TEST(first_list);
    RUN_TEST(first_vector);
    RUN_TEST(first_nil);
    RUN_TEST(rest_list);
    RUN_TEST(rest_nil);
    RUN_TEST(nth_list);
    RUN_TEST(nth_vector);
    RUN_TEST(count_list);
    RUN_TEST(count_vector);
    RUN_TEST(count_nil);
    RUN_TEST(count_map);
    RUN_TEST(conj_list);
    RUN_TEST(conj_vector);
    RUN_TEST(empty_nil);
    RUN_TEST(empty_list);
    RUN_TEST(empty_vector);
    RUN_TEST(get_map);
    RUN_TEST(get_default);
    RUN_TEST(assoc_map);
    RUN_TEST(dissoc_map);
    RUN_TEST(keys_vals);
    RUN_TEST(vals_count);
    RUN_TEST(contains_map);
    RUN_TEST(contains_map_missing);

    /* Predicates */
    RUN_TEST(nil_pred_true);
    RUN_TEST(nil_pred_false);
    RUN_TEST(number_pred_true);
    RUN_TEST(number_pred_false);
    RUN_TEST(string_pred_true);
    RUN_TEST(string_pred_false);
    RUN_TEST(list_pred_true);
    RUN_TEST(list_pred_false);
    RUN_TEST(vector_pred);
    RUN_TEST(map_pred);
    RUN_TEST(fn_pred_native);
    RUN_TEST(fn_pred_false);

    /* Utilities */
    RUN_TEST(not_false);
    RUN_TEST(not_nil);
    RUN_TEST(not_truthy);
    RUN_TEST(str_concat);
    RUN_TEST(str_mixed);
    RUN_TEST(type_fixnum);
    RUN_TEST(type_string);
    RUN_TEST(type_nil);
    RUN_TEST(apply_native);
    RUN_TEST(apply_with_fixed_args);

    /* Closures */
    RUN_TEST(basic_closure);
    RUN_TEST(closure_over_let);
    RUN_TEST(closure_multiple_vars);
    RUN_TEST(nested_closure);
    RUN_TEST(closure_preserves_value);
    RUN_TEST(closure_with_cond);
    RUN_TEST(closure_inline);
    RUN_TEST(closure_self_contained);

    /* Loop/Recur */
    RUN_TEST(loop_basic_countdown);
    RUN_TEST(loop_accumulator);
    RUN_TEST(loop_nested_if);
    RUN_TEST(loop_multiple_bindings);
    RUN_TEST(recur_in_fn);
    RUN_TEST(recur_wrong_arity);
    RUN_TEST(recur_not_tail);
    RUN_TEST(recur_outside_loop);
    RUN_TEST(loop_inside_fn);
    RUN_TEST(loop_let_inside);
    RUN_TEST(let_inside_fn_fix);

    return NULL;
}

int main(void) {
    printf("Testing core native functions...\n\n");

    const char* result = all_tests();

    printf("\nall_tests\n");
    printf("==========================================\n");

    if (result) {
        printf("FAIL: %s\n", result);
        return 1;
    }

    printf("All core native function tests passed!\n");
    return 0;
}
