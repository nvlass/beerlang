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

## Atoms

Atoms provide thread-safe mutable state. Use `@` (deref) to read, `swap!` to update.

```clojure
(def counter (atom 0))
(swap! counter inc)        ; => 1
(swap! counter + 10)       ; => 11
@counter                   ; => 11
(reset! counter 0)         ; => 0
(compare-and-set! counter 0 42)  ; => true
@counter                   ; => 42
```

## Actors

The `beer.hive` library provides Erlang-inspired message-passing actors.

```clojure
(require 'beer.hive :as 'hive)

;; Spawn a counter actor
(def pid (hive/spawn-actor
  (fn [state msg]
    (cond
      (= (:type msg) :inc)  {:state (update state :n inc)}
      (= (:type msg) :get)  {:state state :reply (:n state)}))
  {:n 0}))

;; Fire-and-forget
(hive/send pid {:type :inc})
(hive/send pid {:type :inc})

;; Request-reply
(hive/ask pid {:type :get})  ; => 2

(hive/stop pid)
```

## JSON

```clojure
(require 'beer.json :as 'json)

(json/parse "{\"name\": \"Beerlang\", \"version\": 1}")
; => {"name" "Beerlang" "version" 1}

(json/emit {:langs ["beerlang" "clojure"] :count 2})
; => "{\"langs\":[\"beerlang\",\"clojure\"],\"count\":2}"
```

## Networking

Beerlang includes TCP sockets (`beer.tcp`) and an HTTP server (`beer.http`). See the [API Reference](API.md) for details.

## Network REPL

`beer.nrepl` embeds a TCP REPL server into any running beerlang process —
a long-running script, an HTTP server, a game. Connect from Emacs or `nc`
and eval forms into the live process.

```clojure
;; In your script or application:
(require 'beer.nrepl)
(beer.nrepl/start! 7888)   ; starts listening, returns actual port
```

**From the terminal** (EDN map per line):
```bash
$ printf '{"op" "eval" "code" "(+ 1 2)" "id" "1"}\n' | nc localhost 7888
{:id "1" :value "3"}
{:id "1" :status "done"}
```

**From Emacs** (with `beerlang-repl.el` loaded):
```
M-x beerlang-connect     — connect to localhost:7888
C-x C-e                  — send sexp at point to the live process
C-c C-c                  — send top-level form
C-c C-j                  — connect (shortcut)
C-c C-q                  — disconnect
```

Once connected, all normal eval keybindings (`C-x C-e`, `C-c C-c`,
`C-c C-r`, `C-c C-b`, `C-c C-n`) route to the remote process.
Disconnect with `C-c C-q` to fall back to the local REPL subprocess.

**Thread safety:** nREPL tasks run on worker threads. `swap!` atoms freely;
avoid calling main-thread-only APIs (graphics, UI) directly from the REPL.
Use atoms as the bridge — update state from the REPL, let the main loop
read it on the next iteration.

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
