/* Refcount regression tests for closures, tail calls and spawn.
 *
 * Each probe calls a beerlang function many times through beer_call and
 * checks the live object count is unchanged: a positive delta is a leak,
 * a negative one an over-release. These bugs masked each other before
 * (leaked closures hid a tail-call over-release and a spawn borrow), so
 * they are checked together.
 */

#include <stdio.h>
#include <string.h>
#include "test.h"
#include "beerlang.h"
#include "beer.h"

static BeerState* B;

static const char* SRC =
    "(ns rc (:require [beer.json :as json]))"
    "(defn capture-tail [] (let [x \"cap\"] ((fn [k] (str k x)) 1)))"
    "(defn capture-map [] (let [x 5] (count (map (fn [k] (+ k x)) [1 2 3]))))"
    /* Tail call passing more args than the caller's frame has slots, with
     * heap args: target slots overlap the source arg slots. */
    "(defn g3 [a b c] (count (str a b c)))"
    "(defn tail-overlap [] (let [s \"xy\"] (g3 s \"p\" \"q\")))"
    "(defn emit-map [] (json/emit {:a 1 :b \"hi\" :c [1 2]}))"
    "(defn spawn-capture [] (let [v [1 2 3]] (await (spawn (fn [] (count v))))))"
    "(defn set-ops [] (let [s (into #{} [\"a\" \"bb\" \"a\"]) t (disj (conj s \"ccc\") \"a\")]"
    "  (str (count t) (contains? t \"bb\") (get {s 1} #{\"bb\" \"a\"}) (reduce + 0 (map count t)))))"
    "(defn sort-ops [] (str (sort [\"pear\" \"apple\" \"fig\"]) (sort-by count [\"ccc\" \"a\" \"bb\"])"
    "  (sort > [3 1 2]) (sort #{\"y\" \"x\"})))";

static long delta_over(const char* fname, int iterations, BeerValue* last) {
    BeerValue f = beer_lookup(B, fname);
    BeerValue r = beer_call(B, f, 0, NULL);
    beer_release(r);
    size_t before = memory_stats().objects_alive;
    for (int i = 0; i < iterations; i++) {
        r = beer_call(B, f, 0, NULL);
        if (i < iterations - 1) beer_release(r);
    }
    *last = r;
    beer_release(r);
    size_t after = memory_stats().objects_alive;
    beer_release(f);
    return (long)after - (long)before;
}

TEST(capturing_closure_in_tail_position) {
    BeerValue r;
    ASSERT_EQ(delta_over("rc/capture-tail", 500, &r), 0, "no leak or over-release");
    return NULL;
}

TEST(capturing_closure_passed_to_map) {
    BeerValue r;
    ASSERT_EQ(delta_over("rc/capture-map", 500, &r), 0, "no leak or over-release");
    return NULL;
}

TEST(tail_call_args_overlap_frame) {
    BeerValue f = beer_lookup(B, "rc/tail-overlap");
    BeerValue r = beer_call(B, f, 0, NULL);
    ASSERT(beer_is_int(r) && beer_to_int(r) == 4, "(str \"xy\" \"p\" \"q\") has 4 chars");
    beer_release(r);
    beer_release(f);
    ASSERT_EQ(delta_over("rc/tail-overlap", 500, &r), 0, "no leak or over-release");
    return NULL;
}

TEST(json_emit_map_does_not_leak) {
    BeerValue r;
    ASSERT_EQ(delta_over("rc/emit-map", 500, &r), 0, "no leak or over-release");
    return NULL;
}

TEST(spawned_capturing_closure) {
    BeerValue f = beer_lookup(B, "rc/spawn-capture");
    BeerValue r = beer_call(B, f, 0, NULL);
    ASSERT(beer_is_int(r) && beer_to_int(r) == 3, "spawned closure sees its capture");
    beer_release(r);
    beer_release(f);
    ASSERT_EQ(delta_over("rc/spawn-capture", 200, &r), 0, "no leak or over-release");
    return NULL;
}

TEST(set_operations) {
    BeerValue f = beer_lookup(B, "rc/set-ops");
    BeerValue r = beer_call(B, f, 0, NULL);
    ASSERT(beer_is_string(r) && strcmp(beer_to_cstring(r), "2true15") == 0,
           "set ops give the expected result");
    beer_release(r);
    beer_release(f);
    ASSERT_EQ(delta_over("rc/set-ops", 300, &r), 0, "no leak or over-release");
    return NULL;
}

TEST(sort_native_and_comparator_paths) {
    BeerValue f = beer_lookup(B, "rc/sort-ops");
    BeerValue r = beer_call(B, f, 0, NULL);
    ASSERT(beer_is_string(r) &&
           strcmp(beer_to_cstring(r),
                  "(\"apple\" \"fig\" \"pear\")(\"a\" \"bb\" \"ccc\")(3 2 1)(\"x\" \"y\")") == 0,
           "native and comparator sorts give the expected result");
    beer_release(r);
    beer_release(f);
    ASSERT_EQ(delta_over("rc/sort-ops", 300, &r), 0, "no leak or over-release");
    return NULL;
}

static const char* all_tests(void) {
    B = beer_open();
    beer_add_load_path(B, "lib");
    if (beer_do_string(B, SRC) != 0) {
        printf("load error: %s\n", beer_error(B));
        return "failed to load test source";
    }
    RUN_TEST(capturing_closure_in_tail_position);
    RUN_TEST(capturing_closure_passed_to_map);
    RUN_TEST(tail_call_args_overlap_frame);
    RUN_TEST(json_emit_map_does_not_leak);
    RUN_TEST(spawned_capturing_closure);
    RUN_TEST(set_operations);
    RUN_TEST(sort_native_and_comparator_paths);
    return NULL;
}

int main(void) {
    printf("Testing refcounts of closures, tail calls and spawn...\n");
    RUN_SUITE(all_tests);
    return 0;
}
