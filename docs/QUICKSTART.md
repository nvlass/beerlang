# Beerlang Quick Start Guide

## Setup

1. **Clone and build:**
   ```bash
   cd beerlang
   make
   ```

2. **Start the REPL:**
   ```bash
   ./bl.sh           # with rlwrap (line editing + history)
   ./bin/beerlang     # or directly
   ```

3. **Run a script:**
   ```bash
   ./bin/beerlang my_script.beer
   ```

## Your first session

```clojure
user:1> (+ 1 2 3)
6

user:2> (defn greet [name] (str "Hello, " name "!"))
#<function greet>

user:3> (greet "world")
"Hello, world!"

user:4> (map inc [1 2 3])
(2 3 4)

user:5> (->> (range 1 11) (filter odd?) (reduce +))
25
```

## Defining functions

```clojure
;; Simple function
(defn square [x] (* x x))

;; Multi-arity
(defn greet
  ([name] (greet name "Hello"))
  ([name greeting] (str greeting ", " name "!")))

;; Destructuring
(let [[a b & rest] [1 2 3 4 5]]
  (println a b rest))  ; prints: 1 2 (3 4 5)
```

## Data structures

```clojure
;; Vectors
[1 2 3]
(conj [1 2] 3)          ; => [1 2 3]

;; Maps
{:name "Alice" :age 30}
(assoc {:a 1} :b 2)     ; => {:a 1 :b 2}
(:name {:name "Alice"}) ; => "Alice"

;; Lists
'(1 2 3)
(cons 0 '(1 2 3))       ; => (0 1 2 3)
```

## Project management

Beerlang has built-in project management — no external tools required.

> **Note:** The CLI subcommand interface is experimental and may change in the future.
> In particular, project management commands (`new`, `run`, `build`, `ubertar`) may
> move from built-in subcommands to a beerlang library invoked via `beer -m`.

```bash
# Create a new project
beer new myproject
cd myproject

# Run the project
beer run                    # calls (-main) in the :main namespace

# Build a distributable tar
beer build                  # creates myproject.tar

# Start a project REPL (with src/ and lib/ on the load path)
beer repl
```

A project is defined by a `beer.edn` file (a beerlang map literal):

```clojure
{:name "myproject"
 :version "0.1.0"
 :paths ["src" "lib"]
 :dependencies []
 :main myproject.core}
```

## Namespaces and libraries

```clojure
;; Create a namespace (in a file: mylib/utils.beer)
(ns mylib.utils)
(defn double [x] (* x 2))

;; Use it from another file or the REPL
(require 'mylib.utils :as 'u)
(u/double 21)  ; => 42
```

Set `BEERPATH` to tell beerlang where to find libraries:

```bash
export BEERPATH=/path/to/libs:lib
```

Libraries can also be distributed as `.tar` files — place a tar in any `BEERPATH` directory and its `.beer` files are available to `require`.

## Concurrency

```clojure
;; Spawn a task
(def t (spawn (do (yield) 42)))
(await t)  ; => 42

;; Channels
(let [c (chan)]
  (spawn (>! c "hello"))
  (println (<! c)))  ; prints: hello
```

## Exception handling

```clojure
(try
  (throw (ex-info "oops" {:code 42}))
  (catch e
    (println "Caught:" (:message e)))
  (finally
    (println "cleanup")))
```

## File I/O

```clojure
;; Read/write files
(spit "out.txt" "hello")
(slurp "out.txt")        ; => "hello"

;; Streams
(with-open [f (open "data.txt" :read)]
  (println (read-line f)))
```

## Shell execution

```clojure
(require 'beer.shell :as 'shell)
(let [result (shell/exec "ls" "-la")]
  (println (:out result))
  (println "exit code:" (:exit result)))
```

## Running tests

Beerlang includes a testing framework:

```clojure
;; In mylib/test.beer
(ns mylib.test
  (:require [beer.test :as t]))

(t/deftest addition-test
  (t/is (= 4 (+ 2 2)))
  (t/is (= 0 (+ -1 1))))

(t/run-tests)
```

## Development tools

```bash
make          # Build
make test     # Run unit tests (C) + smoke tests (shell)
make debug    # Debug build (-g -O0)
make repl     # Start REPL
make clean    # Clean build artifacts

beer --help   # Show all CLI commands
beer new      # Create a project
beer run      # Run project
beer build    # Build project tar
```

## Next steps

- [API Reference](API.md) — complete list of functions and macros
- [Design Documents](design/) — language internals and architecture
