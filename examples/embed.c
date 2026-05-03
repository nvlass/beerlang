/*
 * examples/embed.c — Minimal beerlang embedding demo
 *
 * Compile:
 *   make embed
 *   ./bin/embed
 *
 * Or manually:
 *   gcc examples/embed.c -Iinclude -Lbuild -lbeerlang -lm -lpthread -o embed
 */

#include <stdio.h>
#include <stdlib.h>
#include "beer.h"

/* A simple native function exposed to beerlang */
static BeerValue greet(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    if (argc < 1 || !beer_is_string(argv[0])) {
        fprintf(stderr, "greet: expected a string\n");
        return beer_nil();
    }
    printf("Hello from C, %s!\n", beer_to_cstring(argv[0]));
    return beer_nil();
}

int main(void) {
    BeerState* B = beer_open();

    /* -------------------------------------------------------------- */
    /* 1. Run a string of beerlang code                                */
    /* -------------------------------------------------------------- */
    puts("--- beer_do_string ---");
    if (beer_do_string(B, "(println \"hello from beerlang!\")")) {
        fprintf(stderr, "error: %s\n", beer_error(B));
        beer_close(B);
        return 1;
    }

    /* -------------------------------------------------------------- */
    /* 2. Eval an expression and inspect the result                    */
    /* -------------------------------------------------------------- */
    puts("--- beer_eval_expr ---");
    BeerValue result = beer_eval_expr(B, "(+ 1 2 3)");
    if (beer_is_int(result)) {
        printf("(+ 1 2 3) = %lld\n", (long long)beer_to_int(result));
    }
    beer_release(result);

    /* -------------------------------------------------------------- */
    /* 3. Look up and call a beerlang function                         */
    /* -------------------------------------------------------------- */
    puts("--- beer_lookup + beer_call ---");
    beer_do_string(B, "(defn square [x] (* x x))");
    BeerValue sq_fn = beer_lookup(B, "square");
    if (!beer_is_nil(sq_fn)) {
        BeerValue arg = beer_int(7);
        BeerValue r   = beer_call(B, sq_fn, 1, &arg);
        printf("(square 7) = %lld\n", (long long)beer_to_int(r));
        beer_release(r);
        beer_release(sq_fn);
    }

    /* -------------------------------------------------------------- */
    /* 4. Register a C function and call it from beerlang              */
    /* -------------------------------------------------------------- */
    puts("--- beer_register ---");
    beer_register(B, "user", "greet", greet);
    beer_do_string(B, "(user/greet \"world\")");

    /* -------------------------------------------------------------- */
    /* 5. Work with collections returned from beerlang                 */
    /* -------------------------------------------------------------- */
    puts("--- collections ---");
    BeerValue vec = beer_eval_expr(B, "[10 20 30 40 50]");
    printf("vector length = %d\n", beer_length(vec));
    for (int i = 0; i < beer_length(vec); i++) {
        BeerValue elem = beer_nth(vec, i);
        printf("  [%d] = %lld\n", i, (long long)beer_to_int(elem));
    }
    beer_release(vec);

    /* -------------------------------------------------------------- */
    /* 6. Map lookup                                                    */
    /* -------------------------------------------------------------- */
    puts("--- map lookup ---");
    BeerValue m = beer_eval_expr(B, "{:name \"beerlang\" :version 1}");
    BeerValue k = beer_keyword("name");
    BeerValue v = beer_get(m, k);
    if (beer_is_string(v))
        printf(":name = \"%s\"\n", beer_to_cstring(v));
    beer_release(m);

    beer_close(B);
    puts("done.");
    return 0;
}
