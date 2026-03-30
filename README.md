# Beerlang

**A hops-inspired Clojure flavour**

Beerlang is a Clojure-syntax LISP compiled to bytecode and executed on a stack-based virtual machine written in C. It features cooperative multitasking, reference-counted garbage collection, and a rich standard library — all designed to fit in cache.

## Features

- **Clojure syntax** — lists, vectors, hash maps, keywords, destructuring
- **Compile everything** — all code (including REPL input) compiles to bytecode
- **Macros** — `defmacro`, quasiquote, `gensym`; core forms like `defn`, `let`, `cond`, `->`, `->>` are macros
- **Closures & tail calls** — automatic tail call optimization, named self-recursion
- **Exception handling** — `try`/`catch`/`finally`/`throw` with map-based exceptions
- **Namespaces** — `ns`, `require` with `:as` aliases, qualified symbol resolution
- **Cooperative multitasking** — green threads with `spawn`/`yield`/`await`, CSP channels
- **Atoms** — `atom`, `swap!`, `reset!`, `@deref` for managed mutable state
- **Actor system** — `beer.hive` for Erlang-inspired message-passing actors with supervisors
- **Networking** — TCP sockets (`beer.tcp`), HTTP server (`beer.http`), JSON parsing (`beer.json`)
- **Callable non-functions** — keywords, maps, and vectors work in head position
- **Rich stdlib** — `map`, `filter`, `reduce`, `comp`, `partial`, `sort`, string utilities, file I/O, and more
- **Numeric tower** — fixnums, floats, arbitrary-precision bigints with auto-promotion

## Quick taste

```clojure
;; Factorial
(defn factorial [n]
  (loop [i n acc 1]
    (if (<= i 1) acc
      (recur (- i 1) (* acc i)))))

(factorial 20) ; => 2432902008176640000

;; Pipeline
(->> (range 1 11)
     (filter odd?)
     (map #(* % %))
     (reduce +))   ; => 165

;; Concurrent tasks with channels
(let [c (chan)]
  (spawn (>! c 42))
  (<! c))           ; => 42

;; Mutable state with atoms
(def counter (atom 0))
(swap! counter inc)
@counter  ; => 1

;; Keywords and maps as functions
(:name {:name "Beerlang" :type "language"}) ; => "Beerlang"
({:a 1 :b 2} :b)                           ; => 2
```

## Building

```bash
make          # Build beerlang
make test     # Run unit tests + smoke tests
make repl     # Start the REPL
make debug    # Debug build (-g -O0)
make clean    # Clean build artifacts
```

## Requirements

- C compiler (gcc or clang)
- POSIX system (Linux, macOS, BSD)
- Make
- [rlwrap](https://github.com/hanslub42/rlwrap) (optional, for line editing in the REPL)

## Installation

After building, you can run the REPL from anywhere using the `bl.sh` wrapper script. Copy or symlink these to a directory on your `PATH`:

```
bl.sh          # REPL launcher (resolves lib path, wraps with rlwrap)
bin/beerlang   # the binary (or place it next to bl.sh as just "beerlang")
lib/           # standard library (core.beer)
```

Then start the REPL with:

```bash
bl.sh
```

The wrapper sets `BEER_LIB_PATH` automatically so that `lib/core.beer` is found regardless of your working directory.

You can also set `BEERPATH` to a colon-separated list of directories for library lookup:

```bash
export BEERPATH=/path/to/libs:/other/libs
```

Beerlang also supports **tar-based library bundles** — place a `.tar` file in any `BEERPATH` directory and its `.beer` files are transparently available to `require`.

> **Note:** An install script will be added in a future release.

## Documentation

- [API Reference](docs/API.md) — native functions and standard library
- [Quick Start](docs/QUICKSTART.md) — getting started with the REPL
- [Design Documents](docs/design/) — language architecture and internals

## Credits

Designed by [@nvlass](https://github.com/nvlass). Implemented with [Claude Code](https://claude.ai) (Anthropic's AI coding assistant) — nvlass designed the language and architecture; Claude Code wrote the implementation from that specification.

## License

[MIT](LICENSE)
