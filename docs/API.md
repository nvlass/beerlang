# Beerlang API Reference

## Special Forms

These are primitive language constructs handled by the compiler.

### `def`
Define a var in the current namespace.
```clojure
(def x 42)
(def greeting "hello")
```

### `if`
Conditional evaluation. Only evaluates the chosen branch.
```clojure
(if (> x 0) "positive" "non-positive")
```

### `do`
Evaluate expressions sequentially, return the last.
```clojure
(do (println "hello") (println "world") 42)  ;=> 42
```

### `fn`
Create an anonymous function. Optionally named for self-recursion.
```clojure
(fn [x y] (+ x y))
(fn factorial [n] (if (< n 2) 1 (* n (factorial (- n 1)))))
```

### `let*`
Lexical bindings (primitive form — prefer `let` macro).
```clojure
(let* [x 10 y 20] (+ x y))  ;=> 30
```

### `quote`
Prevent evaluation. Reader shorthand: `'form`.
```clojure
(quote (1 2 3))  ;=> (1 2 3)
'foo              ;=> foo
```

### `loop` / `recur`
Structured tail-recursive iteration.
```clojure
(loop [i 0 acc 0]
  (if (< i 10) (recur (+ i 1) (+ acc i)) acc))  ;=> 45
```

### `try` / `catch` / `finally` / `throw`
Exception handling. Only maps can be thrown.
```clojure
(try
  (throw {:type :oops :msg "bad"})
  (catch e (println (:msg e)))
  (finally (println "done")))
```

### `defmacro`
Define a macro.
```clojure
(defmacro unless [test & body]
  `(if (not ~test) (do ~@body)))
```

### `spawn`
Create a new cooperative task.
```clojure
(def t (spawn (+ 1 2)))
(await t)  ;=> 3
```

### `yield`
Yield control to the scheduler.
```clojure
(yield)
```

### `await`
Wait for a task to complete and return its result.
```clojure
(await (spawn (+ 1 2)))  ;=> 3
```

### `>!` / `<!`
Send to / receive from a channel. Blocks the task if needed.
```clojure
(def ch (chan 1))
(>! ch 42)
(<! ch)  ;=> 42
```

---

## Callable Non-Functions

Keywords, hash maps, and vectors can be used in head (call) position, like Clojure's `IFn`:

```clojure
;; Keyword as map lookup (1-2 args)
(:foo {:foo 42})          ;=> 42
(:missing {:a 1} "nope")  ;=> "nope"

;; Map as lookup function (1-2 args)
({:a 1 :b 2} :b)          ;=> 2
({:a 1} :missing "nope")   ;=> "nope"

;; Vector as index function (1 arg)
([10 20 30] 1)             ;=> 20

;; Works dynamically — keyword bound to a variable
(let [k :name] (k {:name "alice"}))  ;=> "alice"

;; Useful as higher-order functions
(map :age [{:age 30} {:age 25}])     ;=> (30 25)
```

---

## Native Functions

### Arithmetic

| Function | Description | Example |
|----------|-------------|---------|
| `+` | Addition (variadic) | `(+ 1 2 3)` → `6` |
| `-` | Subtraction / negation | `(- 10 3)` → `7`, `(- 5)` → `-5` |
| `*` | Multiplication (variadic) | `(* 2 3 4)` → `24` |
| `/` | Division (returns float for non-exact) | `(/ 10 3)` → `3.33333`, `(/ 6 2)` → `3` |
| `mod` | Modulus (sign matches divisor) | `(mod -7 3)` → `2` |
| `rem` | Remainder (sign matches dividend) | `(rem -7 3)` → `-1` |
| `quot` | Truncating integer division | `(quot 7 2)` → `3` |

All arithmetic supports fixnum, bigint, and float with automatic promotion.

### Comparison

| Function | Description | Example |
|----------|-------------|---------|
| `=` | Equality (cross-type sequence equality) | `(= [1 2] '(1 2))` → `true` |
| `<` | Less than (variadic, strictly increasing) | `(< 1 2 3)` → `true` |
| `>` | Greater than (variadic, strictly decreasing) | `(> 3 2 1)` → `true` |
| `<=` | Less than or equal | `(<= 1 1 2)` → `true` |
| `>=` | Greater than or equal | `(>= 3 3 1)` → `true` |

### Collections

| Function | Description | Example |
|----------|-------------|---------|
| `list` | Create a list | `(list 1 2 3)` → `(1 2 3)` |
| `vector` | Create a vector | `(vector 1 2 3)` → `[1 2 3]` |
| `hash-map` | Create a map from key-value pairs | `(hash-map :a 1 :b 2)` → `{:a 1 :b 2}` |
| `cons` | Prepend to a sequence | `(cons 0 '(1 2))` → `(0 1 2)` |
| `first` | First element (works on lists, vectors, strings) | `(first [1 2 3])` → `1` |
| `rest` | All but first | `(rest [1 2 3])` → `(2 3)` |
| `nth` | Get element by index | `(nth [10 20 30] 1)` → `20` |
| `count` | Number of elements | `(count [1 2 3])` → `3` |
| `conj` | Add to collection (lists prepend, vectors append) | `(conj [1 2] 3)` → `[1 2 3]` |
| `empty?` | Test if empty | `(empty? [])` → `true` |
| `get` | Get from map/vector with optional default | `(get {:a 1} :a)` → `1` |
| `assoc` | Associate key-value pairs | `(assoc {:a 1} :b 2)` → `{:a 1 :b 2}` |
| `dissoc` | Remove keys from map | `(dissoc {:a 1 :b 2} :b)` → `{:a 1}` |
| `keys` | Map keys as list | `(keys {:a 1 :b 2})` → `(:a :b)` |
| `vals` | Map values as list | `(vals {:a 1 :b 2})` → `(1 2)` |
| `contains?` | Check key existence (maps and vector indices) | `(contains? {:a 1} :a)` → `true` |
| `concat` | Concatenate sequences | `(concat [1 2] [3 4])` → `(1 2 3 4)` |

### Type Predicates

| Function | Description | Example |
|----------|-------------|---------|
| `nil?` | Test for nil | `(nil? nil)` → `true` |
| `number?` | Fixnum, bigint, or float | `(number? 3.14)` → `true` |
| `int?` | Fixnum or bigint | `(int? 42)` → `true` |
| `float?` | Float | `(float? 3.14)` → `true` |
| `string?` | String | `(string? "hi")` → `true` |
| `symbol?` | Symbol | `(symbol? 'foo)` → `true` |
| `keyword?` | Keyword | `(keyword? :foo)` → `true` |
| `char?` | Character | `(char? \a)` → `true` |
| `list?` | List (cons or nil) | `(list? '(1 2))` → `true` |
| `vector?` | Vector | `(vector? [1 2])` → `true` |
| `map?` | Hash map | `(map? {:a 1})` → `true` |
| `fn?` | Function | `(fn? +)` → `true` |
| `stream?` | I/O stream | `(stream? *out*)` → `true` |
| `task?` | Task | `(task? (spawn 1))` → `true` |
| `channel?` | Channel | `(channel? (chan))` → `true` |

### String Functions

| Function | Description | Example |
|----------|-------------|---------|
| `str` | Concatenate as string | `(str "hi" " " 42)` → `"hi 42"` |
| `subs` | Substring | `(subs "hello" 1 3)` → `"el"` |
| `str/upper-case` | Uppercase | `(str/upper-case "hi")` → `"HI"` |
| `str/lower-case` | Lowercase | `(str/lower-case "HI")` → `"hi"` |
| `str/trim` | Trim whitespace | `(str/trim "  hi  ")` → `"hi"` |
| `str/join` | Join sequence with separator | `(str/join ", " [1 2 3])` → `"1, 2, 3"` |
| `str/split` | Split string | `(str/split "a,b,c" ",")` → `["a" "b" "c"]` |
| `str/includes?` | Substring check | `(str/includes? "hello" "ell")` → `true` |
| `str/starts-with?` | Prefix check | `(str/starts-with? "hello" "he")` → `true` |
| `str/ends-with?` | Suffix check | `(str/ends-with? "hello" "lo")` → `true` |
| `str/replace` | Replace all occurrences | `(str/replace "aabb" "a" "x")` → `"xxbb"` |

Strings also work as sequences with `first`, `rest`, `nth`, `count`, `empty?`, and `map`.

### I/O

| Function | Description | Example |
|----------|-------------|---------|
| `print` | Print values (display mode) | `(print "x=" 42)` |
| `println` | Print values with newline | `(println "hello")` |
| `pr-str` | Readable string representation | `(pr-str "hi")` → `"\"hi\""` |
| `prn` | Print readable with newline | `(prn {:a 1})` |
| `open` | Open file stream | `(open "f.txt" :read)` |
| `close` | Close stream | `(close s)` |
| `read-line` | Read line from stream or stdin | `(read-line)` |
| `write` | Write string to stream | `(write *out* "hi")` |
| `flush` | Flush stream buffer | `(flush *out*)` |
| `slurp` | Read entire file as string | `(slurp "f.txt")` |
| `spit` | Write string to file | `(spit "f.txt" "data")` |

`spit` supports `:append true`: `(spit "f.txt" "more" :append true)`

### Utility

| Function | Description | Example |
|----------|-------------|---------|
| `not` | Boolean negation | `(not false)` → `true` |
| `symbol` | Create symbol from string | `(symbol "foo")` → `foo` |
| `gensym` | Generate unique symbol | `(gensym "tmp")` → `tmp42` |
| `type` | Type as keyword | `(type 42)` → `:fixnum` |
| `float` | Coerce to float | `(float 3)` → `3.0` |
| `int` | Coerce to fixnum (truncate) | `(int 3.7)` → `3` |
| `apply` | Apply function to arg list | `(apply + [1 2 3])` → `6` |
| `macroexpand-1` | Expand macro once | `(macroexpand-1 '(when true 1))` |
| `macroexpand` | Fully expand macros | `(macroexpand '(when true 1))` |
| `set-macro!` | Mark a var as a macro | `(set-macro! 'my-macro)` |
| `in-ns` | Switch/create namespace | `(in-ns 'my.ns)` |
| `require` | Load namespace with optional alias | `(require 'foo.bar :as 'fb)` |
| `load` | Load and execute a .beer file | `(load "path/to/file.beer")` |

### Concurrency

| Function | Description | Example |
|----------|-------------|---------|
| `chan` | Create channel (unbuffered or buffered) | `(chan)`, `(chan 10)` |
| `close!` | Close channel | `(close! ch)` |

Use special forms `>!`, `<!`, `spawn`, `await`, `yield` for task/channel operations.

---

## Standard Library (lib/core.beer)

### Macros

| Macro | Description | Example |
|-------|-------------|---------|
| `defn` | Define named function (multi-arity supported) | `(defn add [a b] (+ a b))` |
| `when` | Conditional without else branch | `(when (> x 0) (println "pos"))` |
| `and` | Short-circuit logical AND | `(and true 42)` → `42` |
| `or` | Short-circuit logical OR | `(or nil false 42)` → `42` |
| `cond` | Multi-branch conditional | `(cond (< x 0) "neg" (> x 0) "pos" :else "zero")` |
| `->` | Thread-first | `(-> 5 (- 3) (* 2))` → `4` |
| `->>` | Thread-last | `(->> [1 2 3] (map inc) (filter odd?))` → `(3)` |
| `let` | Destructuring let bindings | `(let [[a b] [1 2]] (+ a b))` → `3` |
| `with-open` | Auto-close resource | `(with-open [f (open "x" :read)] (read-line f))` |
| `ns` | Declare namespace with requires | `(ns my.lib (:require [other :as o]))` |

### Numeric

| Function | Description | Example |
|----------|-------------|---------|
| `inc` | Increment by 1 | `(inc 5)` → `6` |
| `dec` | Decrement by 1 | `(dec 5)` → `4` |
| `zero?` | Test for zero | `(zero? 0)` → `true` |
| `pos?` | Test positive | `(pos? 5)` → `true` |
| `neg?` | Test negative | `(neg? -1)` → `true` |
| `even?` | Test even | `(even? 4)` → `true` |
| `odd?` | Test odd | `(odd? 3)` → `true` |
| `max` | Maximum of values | `(max 1 3 2)` → `3` |
| `min` | Minimum of values | `(min 1 3 2)` → `1` |
| `abs` | Absolute value | `(abs -5)` → `5` |

### Comparison

| Function | Description | Example |
|----------|-------------|---------|
| `not=` | Not equal | `(not= 1 2)` → `true` |

### Function Utilities

| Function | Description | Example |
|----------|-------------|---------|
| `identity` | Return argument unchanged | `(identity 42)` → `42` |
| `constantly` | Return a function that always returns x | `((constantly 5) :any)` → `5` |
| `complement` | Negate a predicate | `((complement even?) 3)` → `true` |
| `comp` | Compose functions | `((comp inc inc) 0)` → `2` |
| `partial` | Partial application | `((partial + 10) 5)` → `15` |
| `juxt` | Apply multiple fns, return vector | `((juxt inc dec) 5)` → `[6 4]` |

### Sequence Functions

| Function | Description | Example |
|----------|-------------|---------|
| `map` | Transform each element | `(map inc [1 2 3])` → `(2 3 4)` |
| `filter` | Keep elements matching predicate | `(filter even? [1 2 3 4])` → `(2 4)` |
| `reduce` | Fold sequence with function | `(reduce + 0 [1 2 3])` → `6` |
| `second` | Second element | `(second [1 2 3])` → `2` |
| `ffirst` | First of first | `(ffirst [[1 2] [3]])` → `1` |
| `last` | Last element | `(last [1 2 3])` → `3` |
| `butlast` | All but last | `(butlast [1 2 3])` → `(1 2)` |
| `take` | Take first n elements | `(take 2 [1 2 3])` → `(1 2)` |
| `drop` | Drop first n elements | `(drop 2 [1 2 3])` → `(3)` |
| `take-while` | Take while predicate holds | `(take-while pos? [3 2 -1 4])` → `(3 2)` |
| `drop-while` | Drop while predicate holds | `(drop-while pos? [3 2 -1 4])` → `(-1 4)` |
| `partition` | Split into groups of n | `(partition 2 [1 2 3 4])` → `((1 2) (3 4))` |
| `range` | Generate number range | `(range 5)` → `(0 1 2 3 4)` |
| `repeat` | Repeat value n times | `(repeat 3 "x")` → `("x" "x" "x")` |
| `repeatedly` | Call fn n times | `(repeatedly 3 #(gensym))` |
| `reverse` | Reverse sequence | `(reverse [1 2 3])` → `(3 2 1)` |
| `into` | Pour elements into collection | `(into [] '(1 2 3))` → `[1 2 3]` |
| `some` | First truthy predicate result | `(some even? [1 3 4])` → `true` |
| `every?` | All match predicate | `(every? pos? [1 2 3])` → `true` |
| `not-any?` | None match predicate | `(not-any? neg? [1 2 3])` → `true` |
| `not-every?` | Not all match | `(not-every? even? [1 2])` → `true` |
| `frequencies` | Count occurrences | `(frequencies [:a :b :a])` → `{:a 2 :b 1}` |
| `group-by` | Group by function result | `(group-by even? [1 2 3 4])` → `{false (1 3) true (2 4)}` |
| `mapcat` | Map then concat | `(mapcat reverse [[1 2] [3 4]])` → `(2 1 4 3)` |
| `interleave` | Interleave two sequences | `(interleave [:a :b] [1 2])` → `(:a 1 :b 2)` |
| `zipmap` | Zip keys and values into map | `(zipmap [:a :b] [1 2])` → `{:a 1 :b 2}` |

### Map Functions

| Function | Description | Example |
|----------|-------------|---------|
| `get-in` | Nested lookup | `(get-in {:a {:b 1}} [:a :b])` → `1` |
| `assoc-in` | Nested associate | `(assoc-in {} [:a :b] 1)` → `{:a {:b 1}}` |
| `update` | Update value at key | `(update {:a 1} :a inc)` → `{:a 2}` |
| `update-in` | Nested update | `(update-in {:a {:b 1}} [:a :b] inc)` → `{:a {:b 2}}` |
| `merge` | Merge maps (last wins) | `(merge {:a 1} {:b 2})` → `{:a 1 :b 2}` |
| `select-keys` | Keep only specified keys | `(select-keys {:a 1 :b 2 :c 3} [:a :c])` → `{:a 1 :c 3}` |

### Sorting and Dedup

| Function | Description | Example |
|----------|-------------|---------|
| `sort` | Sort sequence (merge sort) | `(sort [3 1 2])` → `(1 2 3)` |
| `sort-by` | Sort by key function | `(sort-by count ["aa" "b" "ccc"])` → `("b" "aa" "ccc")` |
| `flatten` | Flatten nested sequences | `(flatten [[1 [2]] 3])` → `(1 2 3)` |
| `distinct` | Remove duplicates | `(distinct [1 2 1 3])` → `(1 2 3)` |

### Exception Helpers

| Function | Description | Example |
|----------|-------------|---------|
| `ex-info` | Create exception map | `(ex-info "oops" {:code 42})` → `{:type :error :message "oops" :data {:code 42}}` |

---

## Special Vars

| Var | Description |
|-----|-------------|
| `*ns*` | Current namespace name (symbol) |
| `*in*` | Standard input stream |
| `*out*` | Standard output stream |
| `*err*` | Standard error stream |
| `*loaded-libs*` | Map of loaded namespace files |
| `*load-path*` | Vector of library search paths (default: `["lib/"]`) |
