/* WASM REPL entry point — exported functions called from JavaScript.
 *
 * Stdout capture is handled by the JS wrapper via Module.print — no fmemopen
 * needed. This file only exports init/eval/free to JS. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#include "beer.h"

static BeerState* B = NULL;

EMSCRIPTEN_KEEPALIVE
void beer_wasm_init(void) {
    if (B) return;
    B = beer_open();
}

/* JSON-escape src into dst (dst must be large enough). */
static void json_escape(char* dst, size_t dsz, const char* src) {
    size_t di = 0;
    for (const char* s = src; *s && di + 6 < dsz; s++) {
        switch (*s) {
            case '"':  dst[di++] = '\\'; dst[di++] = '"';  break;
            case '\\': dst[di++] = '\\'; dst[di++] = '\\'; break;
            case '\n': dst[di++] = '\\'; dst[di++] = 'n';  break;
            case '\r': dst[di++] = '\\'; dst[di++] = 'r';  break;
            case '\t': dst[di++] = '\\'; dst[di++] = 't';  break;
            default:   dst[di++] = *s;                     break;
        }
    }
    dst[di] = '\0';
}

/* Evaluate a source string. Returns a malloc'd JSON string:
 *   {"ok":true,  "value":"<pr-str result>"}
 *   {"ok":false, "error":"<error message>"}
 * Stdout output (from println etc.) is captured by the JS Module.print hook
 * and is NOT included here — JS merges it into the display.
 * Caller must pass the pointer to beer_wasm_free() when done. */
EMSCRIPTEN_KEEPALIVE
char* beer_wasm_eval(const char* src) {
    if (!B) beer_wasm_init();

    beer_clear_error(B);
    BeerValue result = beer_eval_expr(B, src);

    const char* err = beer_error(B);

    char val_buf[8192] = "";
    if (!beer_is_nil(result) && !err) {
        const char* val_str = beer_to_cstring(result);
        if (val_str) {
            snprintf(val_buf, sizeof(val_buf), "%s", val_str);
        } else {
            /* pr-str the value */
            BeerValue pr_fn      = beer_lookup(B, "beer.core/pr-str");
            BeerValue pr_args[1] = { result };
            BeerValue pr_result  = beer_call(B, pr_fn, 1, pr_args);
            const char* ps = beer_to_cstring(pr_result);
            if (ps) snprintf(val_buf, sizeof(val_buf), "%s", ps);
            beer_release(pr_result);
            beer_release(pr_fn);
        }
    }
    beer_release(result);

    char esc_val[16384];
    char esc_err[2048];
    json_escape(esc_val, sizeof(esc_val), val_buf);
    json_escape(esc_err, sizeof(esc_err), err ? err : "");

    size_t cap = sizeof(esc_val) + sizeof(esc_err) + 64;
    char*  json = malloc(cap);
    if (!json) return strdup("{\"ok\":false,\"error\":\"OOM\"}");

    if (err) {
        snprintf(json, cap, "{\"ok\":false,\"error\":\"%s\"}", esc_err);
    } else {
        snprintf(json, cap, "{\"ok\":true,\"value\":\"%s\"}", esc_val);
    }
    return json;
}

EMSCRIPTEN_KEEPALIVE
void beer_wasm_free(char* ptr) {
    free(ptr);
}
