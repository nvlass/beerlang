/* examples/embedded_http/main.c
 *
 * C host for the embedded_http example: CivetWeb owns the socket/HTTP
 * parsing layer, beerlang (via libbeerlang) owns routing + handler logic.
 *
 * HARD CONSTRAINT, not a tunable: num_threads is fixed at 1. beerlang's
 * runtime is a process-wide singleton (global_namespace_registry,
 * global_scheduler, the symbol/keyword intern tables, and non-atomic
 * refcounting) -- see README.md for the full explanation. Only one OS
 * thread may ever call into the VM, so CivetWeb must not be allowed to
 * run its request handler on more than one worker thread. This is why
 * the high-throughput, multi-core story is a separate prefork example
 * (examples/embedded_http_prefork, one OS process per worker), not a
 * bigger num_threads here.
 *
 * Build: see Makefile. Run: see README.md (sets BEERPATH).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "beer.h"
#include "hashmap.h"
#include "vector.h"
#include "civetweb.h"

#define DEFAULT_PORT "8080"

/* ----------------------------------------------------------------
 * host/request-id native -- demonstrates the C-provides-a-native-to-
 * beerlang half of the embedding API (examples/embed.c shows the same
 * pattern with its `greet` native). Safe without atomics/locks only
 * because num_threads=1 guarantees a single calling thread.
 * ---------------------------------------------------------------- */
static uint64_t g_request_counter = 0;

static BeerValue native_request_id(BeerVM *vm, int argc, BeerValue *argv) {
    (void)vm; (void)argc; (void)argv;
    return beer_int((int64_t)(++g_request_counter));
}

/* ----------------------------------------------------------------
 * Request map construction (C -> beerlang).
 *
 * beer.h deliberately has no map/vector constructors (it's a thin,
 * read-mostly public surface); building a map from C means reaching
 * past it into hashmap.h directly. BeerValue is bit-identical to the
 * internal Value type (see beer.h), so this is safe.
 *
 * hashmap_set() is MUTATING. It is only safe here because this map was
 * just built and has not been shared with anything yet. Never call
 * hashmap_set() on a map beerlang hands back to us (see response
 * handling below) -- it may share HAMT structure with other live data.
 * ---------------------------------------------------------------- */
static void lowercase_into(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static Value build_request_map(struct mg_connection *conn) {
    const struct mg_request_info *ri = mg_get_request_info(conn);

    Value req = hashmap_create_default();

    /* :method -- lowercase keyword (:get, not "GET"), matching
     * lib/beer/http.beer's (keyword (str/lower-case s)) convention. */
    char method_lc[16];
    lowercase_into(method_lc, ri->request_method, sizeof(method_lc));
    {
        Value k = beer_keyword("method");
        Value v = beer_keyword(method_lc);
        hashmap_set(req, k, v);
        beer_release(k);
        beer_release(v);
    }

    /* :uri -- raw string, matching beer.http's :uri (not :path). */
    {
        Value k = beer_keyword("uri");
        Value v = beer_string(ri->request_uri ? ri->request_uri : "/");
        hashmap_set(req, k, v);
        beer_release(k);
        beer_release(v);
    }

    /* :headers -- map with lowercased string keys (civetweb preserves
     * wire case; beer.http's convention lowercases on parse). */
    {
        Value headers = hashmap_create_default();
        for (int i = 0; i < ri->num_headers; i++) {
            char name_lc[256];
            lowercase_into(name_lc, ri->http_headers[i].name, sizeof(name_lc));
            Value hk = beer_string(name_lc);
            Value hv = beer_string(ri->http_headers[i].value);
            hashmap_set(headers, hk, hv);
            beer_release(hk);
            beer_release(hv);
        }
        Value k = beer_keyword("headers");
        hashmap_set(req, k, headers);
        beer_release(k);
        beer_release(headers);
    }

    /* :body -- read the full request body, if any. */
    {
        Value k = beer_keyword("body");
        Value v;
        long long len = ri->content_length;
        if (len > 0) {
            char *buf = malloc((size_t)len + 1);
            long long total = 0;
            while (total < len) {
                int n = mg_read(conn, buf + total, (size_t)(len - total));
                if (n <= 0) break;
                total += n;
            }
            buf[total] = '\0';
            v = beer_string(buf);
            free(buf);
        } else {
            v = beer_string("");
        }
        hashmap_set(req, k, v);
        beer_release(k);
        beer_release(v);
    }

    return req;
}

/* ----------------------------------------------------------------
 * Response handling (beerlang -> C). Read-only: hashmap_get/
 * hashmap_keys never mutate, so this is safe even though the response
 * map may share structure with other live beerlang data.
 * ---------------------------------------------------------------- */
static void send_response(struct mg_connection *conn, BeerValue resp) {
    int status = 200;
    const char *body = "";
    Value headers = VALUE_NIL;

    if (!beer_is_nil(resp) && beer_is_map(resp)) {
        Value k_status = beer_keyword("status");
        Value status_val = hashmap_get(resp, k_status);
        beer_release(k_status);
        if (beer_is_int(status_val)) {
            status = (int)beer_to_int(status_val);
        }

        Value k_headers = beer_keyword("headers");
        headers = hashmap_get(resp, k_headers);
        beer_release(k_headers);

        Value k_body = beer_keyword("body");
        Value body_val = hashmap_get(resp, k_body);
        beer_release(k_body);
        if (beer_is_string(body_val)) {
            body = beer_to_cstring(body_val);
        }
    } else {
        status = 500;
        body = "handler error\n";
    }

    mg_printf(conn, "HTTP/1.1 %d %s\r\n", status,
              mg_get_response_code_text(conn, status));

    int have_content_type = 0;
    if (!beer_is_nil(headers) && beer_is_map(headers)) {
        Value keys = hashmap_keys(headers);
        size_t n = vector_length(keys);
        for (size_t i = 0; i < n; i++) {
            Value hk = vector_get(keys, i);
            Value hv = hashmap_get(headers, hk);
            if (beer_is_string(hk) && beer_is_string(hv)) {
                const char *name = beer_to_cstring(hk);
                mg_printf(conn, "%s: %s\r\n", name, beer_to_cstring(hv));
                if (strcasecmp(name, "content-type") == 0) have_content_type = 1;
            }
        }
        object_release(keys);
    }
    if (!have_content_type) {
        mg_printf(conn, "Content-Type: text/plain\r\n");
    }
    mg_printf(conn, "Content-Length: %zu\r\n\r\n", strlen(body));
    mg_write(conn, body, strlen(body));
}

/* ----------------------------------------------------------------
 * CivetWeb request callback -- the one place per-request C and
 * beerlang meet. Registered for "/" so it catches every path; app.beer
 * does its own cond-based routing on (:uri req)/(:method req).
 * ---------------------------------------------------------------- */
static BeerState *g_beer = NULL;
static BeerValue g_handler;

static int request_handler(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;

    Value req = build_request_map(conn);
    BeerValue resp = beer_call(g_beer, g_handler, 1, &req);
    object_release(req);

    send_response(conn, resp);
    beer_release(resp);

    return 1; /* handled */
}

int main(int argc, char **argv) {
    const char *port = (argc > 1) ? argv[1] : DEFAULT_PORT;

    g_beer = beer_open();

    if (beer_do_file(g_beer, "app.beer") != 0) {
        fprintf(stderr, "Failed to load app.beer: %s\n", beer_error(g_beer));
        beer_close(g_beer);
        return 1;
    }

    beer_register(g_beer, "host", "request-id", native_request_id);

    g_handler = beer_lookup(g_beer, "app/handle-request");
    if (beer_is_nil(g_handler)) {
        fprintf(stderr, "app/handle-request not found -- did app.beer load correctly?\n");
        beer_close(g_beer);
        return 1;
    }

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    /* num_threads=1 is a correctness requirement, not a performance knob
     * -- see the comment at the top of this file. */
    const char *options[] = {
        "listening_ports", port,
        "num_threads", "1",
        NULL
    };

    struct mg_context *ctx = mg_start(&callbacks, NULL, options);
    if (!ctx) {
        fprintf(stderr, "mg_start failed (port %s already in use?)\n", port);
        beer_release(g_handler);
        beer_close(g_beer);
        return 1;
    }
    mg_set_request_handler(ctx, "/", request_handler, NULL);

    printf("embedded_http listening on :%s (num_threads=1 -- see README.md)\n", port);
    printf("try: curl localhost:%s/  and  curl localhost:%s/api/time\n", port, port);
    printf("Ctrl+C to stop\n");

    /* mg_start already runs its own thread(s) internally; block main. */
    for (;;) {
        getchar();
    }

    mg_stop(ctx);
    beer_release(g_handler);
    beer_close(g_beer);
    return 0;
}
