# beerlang + CivetWeb embedded HTTP server

A C host embeds beerlang via `libbeerlang`; CivetWeb (vendored, MIT licensed)
owns the socket and HTTP-parsing layer, beerlang owns routing and handlers.

```
C main()
  └─ beer_open() + beer_register("host", "request-id", ...)
  └─ beer_do_file("app.beer") + beer_lookup("app/handle-request")
  └─ mg_start(..., num_threads=1) + mg_set_request_handler("/", ...)
  └─ per request: build request map → beer_call(handler, req) → write response
```

`app.beer` does its own routing via `cond` on `(:uri req)`/`(:method req)` —
there's no router abstraction here, matching the existing pure-beerlang
examples (`examples/hello_server.beer`, `examples/json_api.beer`). The
request/response map shape matches `beer.http`'s Ring-style convention
(`lib/beer/http.beer`) so handler code looks the same regardless of which
transport is driving it:

```clojure
;; request:  {:method :get  :uri "/"  :headers {"..." "..."}  :body "..."}
;; response: {:status 200   :headers {"..." "..."}            :body "..."}
```

## Why `num_threads=1` is a correctness requirement, not a tunable

beerlang's runtime is a **process-wide singleton**, not instance-isolated.
`beer_open()` initializes `global_namespace_registry`, `global_scheduler`,
and the symbol/keyword intern tables as plain C file-scope globals (see
`src/lib/beer.c`, `src/runtime/namespace.c`, `src/types/symbol.c`) — a
second `beer_open()` in the same process would just share that same state,
not create an isolated second VM. Reference counting
(`object_retain`/`object_release`, `src/memory/alloc.c`) is plain
non-atomic `refcount++`/`refcount--`.

Two OS threads calling into the VM concurrently would race on the shared
symbol table, the namespace registry, and every object's refcount — this
isn't a performance caveat, it's a correctness one. CivetWeb's `num_threads`
option is fixed at `1` in `main.c` for exactly this reason. **Do not raise
it** without first making the beerlang runtime thread-safe (thread-local or
per-handle globals, atomic refcounts) — a real core-runtime change, not
something to improvise here.

For real multi-core throughput today, without touching beerlang's C
runtime, see `examples/embedded_http_prefork/` (planned follow-up): N OS
*processes*, each with its own independent `beer_open()`, sharing a
listening port via `SO_REUSEPORT`. One process per worker, not one thread
per worker — process isolation sidesteps the shared-globals problem for
free, at the cost of per-worker memory (mitigated by `fork()`'s
copy-on-write pages).

## Why CivetWeb, not Mongoose or Drogon

- **Drogon** (the originally proposed C++ framework) needs a mandatory
  dependency chain (jsoncpp, libuuid, zlib, trantor as a submodule, CMake
  ≥3.5) that breaks from beerlang's single-Makefile, minimal-vendored-deps
  build style, and it's a full application framework (ORM, sessions,
  plugins) when all this example needs is routing + a socket.
- **Mongoose** looks like the obvious "single-file embeddable C server"
  pick, but Cesanta dual-licenses it GPLv2/commercial — wrong fit for a
  permissively licensed example meant to be freely copied.
- **CivetWeb** is the MIT-licensed fork of Mongoose (forked in 2013, before
  the relicense), architecturally identical: single `civetweb.c`/`civetweb.h`
  plus a handful of small `.inl` files it includes internally, zero
  mandatory dependencies, built with `-DNO_SSL` here (no HTTPS needed).

## Dependencies

| Dependency | Install |
|------------|---------|
| beerlang (`libbeerlang.a`) | `make libbeerlang` in the repo root |
| CivetWeb | vendored in `vendor/` — nothing to install |

## Build & run

```bash
# 1. Build libbeerlang (from repo root, once)
cd ../..
make libbeerlang

# 2. Build and run the server
cd examples/embedded_http
make run
```

`make run` sets `BEERPATH` so `app.beer`'s `(:require [beer.json ...])` can
find the standard library.

```bash
curl localhost:8080/
curl localhost:8080/api/time
curl localhost:8080/does-not-exist   # 404
```

Pass a different port: `./embedded_http 9090`.

## Sanity throughput check (optional)

Not a rigorous benchmark — just confirms the embedding isn't pathologically
slow before anyone calls this "high performance":

```bash
wrk -t1 -c50 -d5s http://localhost:8080/
```
