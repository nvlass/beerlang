#!/bin/bash
# Beerlang REPL smoke tests
# Run: bash tests/smoke_test.sh
# Pipes expressions into the REPL binary and checks output.

BEER=./bin/beerlang
PASS=0
FAIL=0

if [ ! -x "$BEER" ]; then
    echo "ERROR: $BEER not found. Run 'make' first."
    exit 1
fi

check() {
    local expr="$1" expected="$2"
    # Pipe expression then exit, filter noise, grab last non-empty line
    actual=$(printf '%s\n(exit)\n' "$expr" | "$BEER" 2>/dev/null \
        | sed -n 's/^[a-zA-Z._]*:[0-9]*> //p' \
        | grep -v '^$\|^Goodbye' \
        | tail -1)
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: $expr => '$actual' (expected '$expected')"
        FAIL=$((FAIL + 1))
    fi
}

# check_multi: send multiple expressions, check last result
check_multi() {
    local exprs="$1" expected="$2" label="$3"
    actual=$(printf '%s\n(exit)\n' "$exprs" | "$BEER" 2>/dev/null \
        | sed -n 's/^[a-zA-Z._]*:[0-9]*> //p' \
        | grep -v '^$\|^Goodbye' \
        | tail -1)
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: $label => '$actual' (expected '$expected')"
        FAIL=$((FAIL + 1))
    fi
}

echo "Running REPL smoke tests..."

# --- Arithmetic (reader fix: / and - as symbols) ---
check '(+ 1 2)'                  '3'
check '(- 10 3)'                 '7'
check '(* 4 5)'                  '20'
check '(/ 10 2)'                 '5'
check '(let [x 10] (/ x 2))'    '5'
check '(let [n 5] (- n 1))'     '4'
check '(- (+ 3 2) 1)'           '4'
check '(/ (* 6 2) 3)'           '4'

# --- Collections ---
check '(list 1 2 3)'           '(1 2 3)'
check '(list)'                 'nil'
check '(vector 1 2 3)'         '[1 2 3]'
check '(first (list 10 20))'   '10'
check '(first (vector 10 20))' '10'
check '(first nil)'            'nil'
check '(count (list 1 2 3))'   '3'
check '(count (vector 1 2))'   '2'
check '(count nil)'            '0'
check '(nth (list 10 20 30) 1)'  '20'
check '(nth (vector 10 20 30) 2)' '30'
check '(cons 0 (list 1 2))'   '(0 1 2)'
check '(empty? nil)'           'true'
check '(empty? (list 1))'      'false'
check '(empty? (vector))'      'true'

# --- Predicates ---
check '(nil? nil)'             'true'
check '(nil? 0)'               'false'
check '(number? 42)'           'true'
check '(number? "x")'          'false'
check '(string? "hi")'         'true'
check '(list? (list 1))'       'true'
check '(list? (vector 1))'     'false'
check '(fn? +)'                'true'
check '(fn? 42)'               'false'

# --- Utilities ---
check '(not false)'            'true'
check '(not nil)'              'true'
check '(not 42)'               'false'
check '(str "a" "b")'          '"ab"'
check '(type 42)'              ':fixnum'
check '(type "hi")'            ':string'
check '(apply + (list 1 2 3))' '6'

# --- Closures ---
check '((fn [x] ((fn [y] (+ x y)) 5)) 10)' '15'
check '((let [x 10] (fn [y] (+ x y))) 5)' '15'
check '((((fn [x] (fn [y] (fn [z] (+ x (+ y z))))) 100) 20) 3)' '123'

# --- Loop/Recur ---
check '(loop [i 5] (if (= i 0) i (recur (+ i -1))))' '0'
check '(loop [i 1 acc 1] (if (> i 10) acc (recur (+ i 1) (* acc i))))' '3628800'
check '(loop [n 10 a 0 b 1] (if (= n 0) a (recur (+ n -1) b (+ a b))))' '55'
check '((fn fact [n acc] (if (< n 2) acc (recur (+ n -1) (* acc n)))) 10 1)' '3628800'
check '((fn [a] (let [x 1] (+ a x))) 10)' '11'
check '(loop [i 4 acc (list)] (if (= i 0) acc (recur (- i 1) (conj acc i))))' '(1 2 3 4)'

# --- Variadic Functions ---
check '((fn [a & rest] rest) 10 20 30)'       '(20 30)'
check '((fn [a & rest] a) 10 20 30)'          '10'
check '((fn [& args] (count args)) 1 2 3)'    '3'
check '((fn [& args] args))'                  'nil'
check '((fn [a b & rest] rest) 1 2)'          'nil'
check '((fn [a b & rest] rest) 1 2 3 4 5)'   '(3 4 5)'
check '((fn [& args] (first args)) 10 20)'    '10'
check '((fn [a & rest] (count rest)) 1 2 3 4)' '3'

# --- Concat ---
check '(concat (list 1 2) (list 3 4))'    '(1 2 3 4)'
check '(concat (list 1) nil (list 2 3))'  '(1 2 3)'
check '(concat)'                          'nil'

# --- Quasiquote ---
check '`42'                                '42'
check '`(a b c)'                           '(a b c)'
check '(let [x 1] `(a ~x b))'            '(a 1 b)'
check '(let [xs (list 2 3)] `(1 ~@xs 4))' '(1 2 3 4)'
check '`(~(+ 1 2))'                       '(3)'

# --- Macros ---
check_multi '(defmacro my-inc [x] (list (quote +) x 1))
(my-inc 5)' '6' 'defmacro my-inc'

check_multi '(defmacro my-when [test body] (list (quote if) test body nil))
(my-when true 42)' '42' 'my-when true'

check_multi '(defmacro my-when2 [test body] (list (quote if) test body nil))
(my-when2 false 42)' 'nil' 'my-when false'

check_multi '(defmacro my-inc3 [x] `(+ ~x 1))
(my-inc3 10)' '11' 'defmacro with quasiquote'

# --- Core Macros (from lib/core.beer) ---
check_multi '(defn square [x] (* x x))
(square 5)' '25' 'defn square'

check_multi '(defn fact [n acc] (if (< n 2) acc (recur (+ n -1) (* acc n))))
(fact 10 1)' '3628800' 'defn with recur'

check '(when true 42)'              '42'
check '(when false 42)'             'nil'
check '(and true true)'             'true'
check '(and true false)'            'false'
check '(and false 999)'             'false'
check '(or nil 42)'                 '42'
check '(or false false nil)'        'nil'
check '(or 1 2)'                    '1'

# --- Vector and map literals ---
check '[1 2 3]'                                '[1 2 3]'
check '[]'                                     '[]'
check '[(+ 1 2) 4]'                            '[3 4]'
check '(first [10 20 30])'                     '10'
check '(count [1 2 3 4])'                      '4'
check '{:a 1}'                                 '{:a 1}'
check '(get {:a 1 :b 2} :b)'                   '2'
check '(get {:a 1 :b 2 [2 3] :x} [2 3])'       ':x'
check '(get {:a 1 :b 2 {:q 1} :x} {:q 1})'     ':x'

# --- cond, ->, ->> macros ---
check '(cond false 1 true 42)'                  '42'
check '(cond false 1 false 2 :else 99)'         '99'
check '(cond false 1)'                          'nil'
check '(-> 10 (- 3))'                           '7'
check '(-> 1 (+ 2) (* 3))'                      '9'
check '(->> 10 (- 3))'                          '-7'
check '(->> 1 (+ 2) (* 3))'                     '9'

# --- let destructuring ---
check '(let [[a b c] [10 20 30]] (+ a b c))'         '60'
check '(let [[x y] [1 2]] (list x y))'                '(1 2)'
check "(let [[a b] '(1 2)] (+ a b))"                  '3'
check '(let [[a & rest] [1 2 3 4]] rest)'              '(2 3 4)'
check '(let [[a b :as all] [1 2 3]] all)'              '[1 2 3]'
check '(let [x 10 [a b] [1 2]] (+ x a b))'            '13'

# --- Cross-type sequence equality ---
check "(= '(1 2 3) [1 2 3])"                     'true'
check "(= [1 2 3] '(1 2 3))"                     'true'
check "(= '(1 2 3) [1 2 4])"                     'false'
check "(= '(1 2) [1 2 3])"                       'false'
check "(= nil [])"                                'true'
check "(= [] nil)"                                'true'

# --- Comparison operators ---
check '(>= 5 3)'                                  'true'
check '(>= 3 3)'                                  'true'
check '(>= 2 3)'                                  'false'
check '(<= 1 2)'                                  'true'
check '(<= 3 3)'                                  'true'
check '(<= 3 2)'                                  'false'
check '(<= 1 2 3 4)'                              'true'
check '(>= 4 3 2 1)'                              'true'

# --- Numeric utilities ---
check '(inc 5)'                                   '6'
check '(dec 5)'                                   '4'
check '(zero? 0)'                                 'true'
check '(zero? 1)'                                 'false'
check '(pos? 1)'                                  'true'
check '(neg? -1)'                                 'true'
check '(even? 4)'                                 'true'
check '(odd? 3)'                                  'true'
check '(mod 10 3)'                                '1'
check '(mod -10 3)'                               '2'
check '(rem -10 3)'                               '-1'
check '(not= 1 2)'                                'true'
check '(not= 1 1)'                                'false'

# --- Function utilities ---
check '(identity 42)'                             '42'
check '((constantly 42) 1 2 3)'                   '42'
check '((complement zero?) 5)'                    'true'
check '((complement zero?) 0)'                    'false'

# --- filter / reduce ---
check '(filter (fn [x] (> x 2)) [1 2 3 4 5])'   '(3 4 5)'
check '(reduce + 0 [1 2 3 4 5])'                 '15'
check "(reduce + 0 '())"                          '0'

# --- apply ---
check '(apply + [1 2 3])'                                '6'
check '(apply + 1 2 [3 4])'                              '10'
check "(apply + '(1 2 3))"                                '6'
check '(apply (fn [a b] (+ a b)) [10 20])'               '30'
check '(apply (fn [& args] (count args)) [1 2 3])'       '3'

# --- map ---
check '(->> [1 2 3 4 5] (map (fn [x] (+ x 1)))))'  '(2 3 4 5 6)'

# --- Fibonacci (exercises recursion, cond, arithmetic) ---
check_multi '(defn fib [x] (cond (= x 0) 1 (= x 1) 1 :else (+ (fib (- x 1)) (fib (- x 2)))))
(fib 8)' '34' 'fibonacci(8)'

# --- Multi-arity defn ---
check_multi '(defn greet ([x] (str "hi " x)) ([x y] (str "hi " x " and " y)))
(greet "alice")' '"hi alice"' 'multi-arity 1-arg'
check_multi '(defn greet2 ([x] (str "hi " x)) ([x y] (str "hi " x " and " y)))
(greet2 "a" "b")' '"hi a and b"' 'multi-arity 2-arg'

# --- More collection utilities ---
check '(second [10 20 30])'                       '20'
check '(last [1 2 3 4 5])'                        '5'
check '(take 3 [1 2 3 4 5])'                      '(1 2 3)'
check '(drop 2 [1 2 3 4 5])'                      '(3 4 5)'
check '(range 5)'                                  '(0 1 2 3 4)'
check '(range 2 5)'                                '(2 3 4)'
check '(reverse [1 2 3])'                          '(3 2 1)'
check '(every? pos? [1 2 3])'                      'true'
check '(every? pos? [1 -2 3])'                     'false'
check '(some even? [1 3 4 5])'                     'true'
check '(some even? [1 3 5])'                       'nil'
check '(into [1 2] [3 4 5])'                       '[1 2 3 4 5]'
check '(get-in {:a {:b 42}} [:a :b])'              '42'
check '(assoc-in {} [:a :b] 42)'                   '{:a {:b 42}}'
check '(update {:x 1} :x inc)'                     '{:x 2}'
check '(zipmap [:a :b] [1 2])'                     '{:b 2, :a 1}'
check '(partition 2 [1 2 3 4])'                    '((1 2) (3 4))'
check '(mapcat (fn [x] (list x x)) [1 2 3])'      '(1 1 2 2 3 3)'

# --- Exception Handling (try/catch/throw) ---
check '(try (throw {:type :error :message "boom"}) (catch e (get e :message)))' '"boom"'
check '(try 42 (catch e nil))'                      '42'
check '(try (+ 1 2) (catch e nil))'                 '3'
check_multi '(defn boom [] (throw {:type :err}))
(try (boom) (catch e (get e :type)))' ':err' 'throw from function'
check '(try (try (throw {:a 1}) (catch e (throw {:b 2}))) (catch e (get e :b)))' '2'
check '(try (throw (ex-info "oops" {:code 42})) (catch e (get e :message)))' '"oops"'
check '(try (throw (ex-info "oops" {:code 42})) (catch e (get (get e :data) :code)))' '42'
check_multi '(defn safe-div [a b] (if (= b 0) (throw {:type :div-by-zero}) (/ a b)))
(try (safe-div 10 0) (catch e (get e :type)))' ':div-by-zero' 'throw from nested fn'
check_multi '(defn safe-div [a b] (if (= b 0) (throw {:type :div-by-zero}) (/ a b)))
(try (safe-div 10 2) (catch e (get e :type)))' '5' 'no throw path'

# --- finally ---
check '(try 42 (finally (+ 1 2)))' '42' 'finally: normal path returns try value'
check '(try (throw (ex-info "err" {})) (catch e 99) (finally (+ 1 2)))' '99' 'finally: catch path returns catch value'
check_multi '(def x 0)
(try 42 (finally (def x 1)))
x' '1' 'finally: side effect runs on normal path'
check_multi '(def y 0)
(try (throw (ex-info "err" {})) (catch e 99) (finally (def y 1)))
y' '1' 'finally: side effect runs on catch path'
check '(try (+ 1 2) (catch e nil) (finally (+ 10 20)))' '3' 'finally: does not change return value with catch'

# --- Readable printing ---
check '"hello"'                                    '"hello"'
check '[1 "a" :b]'                                 '[1 "a" :b]'
check '(list "x" 1)'                               '("x" 1)'
check '{:a "hi"}'                                   '{:a "hi"}'

# --- String as sequence ---
check '(first "hello")'                            '\h'
check '(rest "hello")'                             '(\e \l \l \o)'
check '(count "hello")'                            '5'
check '(count "")'                                 '0'
check '(nth "hello" 1)'                            '\e'
check '(empty? "")'                                'true'
check '(empty? "a")'                               'false'
check '(map identity "abc")'                       '(\a \b \c)'

# --- pr-str / prn ---
check '(pr-str "hi")'                              '"\"hi\""'
check '(pr-str 42)'                                '"42"'
check '(pr-str :a)'                                '":a"'
check '(pr-str {:a 1 :b {:c 2}})'                  '"{:b {:c 2}, :a 1}"'
check '(pr-str [1 2 3 4])'                         '"[1 2 3 4]"'

# --- String functions ---
check '(subs "hello" 1 3)'                         '"el"'
check '(subs "hello" 2)'                           '"llo"'
check '(str/upper-case "hello")'                   '"HELLO"'
check '(str/lower-case "HELLO")'                   '"hello"'
check '(str/trim "  hi  ")'                        '"hi"'
check '(str/join ", " [1 2 3])'                    '"1, 2, 3"'
check '(str/join "-" ["a" "b" "c"])'               '"a-b-c"'
check '(str/split "a,b,c" ",")'                    '("a" "b" "c")'
check '(str/includes? "hello" "ell")'              'true'
check '(str/includes? "hello" "xyz")'              'false'
check '(str/starts-with? "hello" "he")'            'true'
check '(str/starts-with? "hello" "lo")'            'false'
check '(str/ends-with? "hello" "lo")'              'true'
check '(str/ends-with? "hello" "he")'              'false'
check '(str/replace "hello world" "o" "0")'        '"hell0 w0rld"'
check '(char? \a)'                                 'true'
check '(char? 42)'                                 'false'

# --- I/O: stream type ---
check '(stream? *out*)'                'true'
check '(stream? *in*)'                 'true'
check '(stream? *err*)'                'true'
check '(stream? 42)'                   'false'
check '(stream? nil)'                  'false'

# --- I/O: slurp / spit ---
TMPFILE=$(mktemp /tmp/beer_test_XXXXXX)
trap "rm -f $TMPFILE" EXIT

check_multi "(spit \"$TMPFILE\" \"hello world\")
(slurp \"$TMPFILE\")" '"hello world"' "slurp after spit"

check_multi "(spit \"$TMPFILE\" \"line1\")
(spit \"$TMPFILE\" \"line2\n\" :append true)
(slurp \"$TMPFILE\")" '"line1line2\n"' "spit append"

# --- I/O: open / read-line / close ---
check_multi "(spit \"$TMPFILE\" \"aaa\nbbb\nccc\")
(let* [f (open \"$TMPFILE\" :read) l1 (read-line f) l2 (read-line f)] (close f) (str l1 \"-\" l2))" '"aaa-bbb"' "open/read-line/close"

# --- I/O: write to stream ---
check_multi "(let* [f (open \"$TMPFILE\" :write)] (write f \"streamed\") (close f))
(slurp \"$TMPFILE\")" '"streamed"' "write to file stream"

# --- I/O: type predicate on stream ---
check_multi "(let* [f (open \"$TMPFILE\" :read) t (stream? f)] (close f) t)" 'true' "stream? on file"

# --- I/O: with-open macro ---
check_multi "(spit \"$TMPFILE\" \"macro-test\")
(with-open [f (open \"$TMPFILE\" :read)] (read-line f))" '"macro-test"' "with-open macro"

# ===============================
# Namespace / require / in-ns
# ===============================

# --- *ns* variable ---
check "*ns*" "user"
check_multi "(in-ns 'test.ns)
*ns*" "test.ns" "*ns* after in-ns"

# --- beer.core fallback ---
check "(+ 1 2)" "3"   # + is in beer.core, looked up from user

# --- in-ns + core fallback ---
check_multi "(in-ns 'my.test)
(+ 1 1)" "2" "in-ns + core fallback"

# Helper lines: set *load-path* in beer.core for all ns tests
SETLP="(in-ns (quote beer.core))
(def *load-path* [\"tests/fixtures/lib/\"])
(in-ns (quote user))"

# --- require + :as alias ---
check_multi "$SETLP
(require 'test_ns.greeter :as 'g)
g/greeting" '"hello"' "require :as alias"

# --- qualified function call ---
check_multi "$SETLP
(require 'test_ns.greeter :as 'g)
(g/greet \"world\")" '"hello, world!"' "qualified fn call"

# --- require without :as (full qualified name) ---
check_multi "$SETLP
(require 'test_ns.greeter)
test_ns.greeter/greeting" '"hello"' "require full qualified"

# --- double require skip ---
check_multi "$SETLP
(require 'test_ns.greeter :as 'g)
(require 'test_ns.greeter :as 'g2)
g/greeting" '"hello"' "double require skip"

# --- ns macro ---
check_multi "$SETLP
(ns my.app (:require [test_ns.greeter :as g]))
(g/greet \"beer\")" '"hello, beer!"' "ns macro"

# --- Concurrency: spawn/await ---
check '(yield)' 'nil'
check '(await (spawn (fn [] (+ 1 2))))' '3'
check '(await (spawn (fn [] 42)))' '42'
check '(await (spawn (fn [] (+ 10 20))))' '30'
check '(task? (spawn (fn [] 1)))' 'true'
check '(task? 42)' 'false'
check '(channel? (chan))' 'true'
check '(channel? (chan 5))' 'true'
check '(channel? 42)' 'false'

# --- Concurrency: buffered channel ---
check_multi '(def c (chan 1))
(>! c 42)
(<! c)' '42' "buffered channel send/recv"

# --- Concurrency: spawn + channel ---
check_multi '(def c (chan 1))
(spawn (fn [] (>! c 99)))
(<! c)' '99' "spawn sends to channel"

# --- Concurrency: unbuffered channel ---
check_multi '(def c (chan))
(spawn (fn [] (>! c 42)))
(<! c)' '42' "unbuffered channel send/recv"

check_multi '(def c (chan))
(spawn (fn [] (>! c (+ 1 2))))
(<! c)' '3' "unbuffered channel send expression"

# --- Concurrency: close!, channel?, task? ---
check_multi '(def c (chan 1))
(>! c 42)
(close! c)
(<! c)' '42' "close! channel still has buffered value"

check '(channel? (chan 1))' 'true'  "channel? true"
check '(channel? 42)'      'false' "channel? false"
check '(task? (spawn (fn [] 1)))' 'true'  "task? true"
check '(task? 42)'                'false' "task? false"

# --- Concurrency: multiple spawns ---
check_multi '(def c (chan 10))
(spawn (fn [] (>! c 1)))
(spawn (fn [] (>! c 2)))
(spawn (fn [] (>! c 3)))
(+ (<! c) (<! c) (<! c))' '6' "multiple spawns"

# ==========================================
# Float (double) Type
# ==========================================

# Float literals
check '3.14'              '3.14'
check '-2.5'              '-2.5'
check '1.5e2'             '150'
check '1e3'               '1000'

# Float arithmetic
check '(+ 1.5 2.5)'       '4'
check '(- 5.0 2.0)'       '3'
check '(* 2.0 3.0)'       '6'
check '(/ 5.0 2.0)'       '2.5'

# Mixed fixnum/float
check '(+ 1 2.5)'         '3.5'
check '(* 3 1.5)'         '4.5'
check '(- 10 2.5)'        '7.5'

# Division returns float for non-exact
check '(/ 5 2)'           '2.5'
check '(/ 10 5)'          '2'
check '(/ 1 3)'           '0.333333'

# Integer division
check '(quot 7 2)'        '3'
check '(quot -7 2)'       '-3'

# Negation
check '(- 3.14)'          '-3.14'

# Comparison
check '(< 1.5 2.5)'       'true'
check '(> 3.0 2.0)'       'true'
check '(= 2 2.0)'         'true'
check '(<= 1.5 1.5)'      'true'
check '(>= 2.0 1.5)'      'true'

# Type predicates
check '(number? 3.14)'    'true'
check '(float? 3.14)'     'true'
check '(float? 42)'       'false'
check '(int? 42)'         'true'
check '(int? 3.14)'       'false'
check '(type 3.14)'       ':float'

# Coercion
check '(int 3.7)'         '3'
check '(int -2.9)'        '-2'
check '(float 3)'         '3'

# ============================================================
# Callable non-functions (keywords, maps, vectors as IFn)
# ============================================================
check '(:foo {:foo 42 :bar 99})'           '42'
check '(:missing {:a 1})'                  'nil'
check '(:missing {:a 1} "default")'        '"default"'
check '({:a 1 :b 2} :b)'                   '2'
check '({:a 1} :missing)'                  'nil'
check '({:a 1} :missing "nope")'           '"nope"'
check '([10 20 30] 0)'                     '10'
check '([10 20 30] 2)'                     '30'
check_multi '(let [k :name] (k {:name "alice"}))' '"alice"'
check '(map :age [{:age 30} {:age 25}])'   '(30 25)'
check '(map {:a 1 :b 2} [:a :b :c])'       '(1 2 nil)'

# ============================================================
# Additional stdlib functions
# ============================================================
check '(max 3 1 4 1 5 9)'                  '9'
check '(min 3 1 4 1 5 9)'                  '1'
check '(max 42)'                            '42'
check '(abs -5)'                            '5'
check '(abs 5)'                             '5'
check_multi '((comp inc inc) 0)'            '2'
check_multi '((partial + 10) 5)'            '15'
check_multi '((juxt inc dec) 5)'            '[6 4]'
check '(flatten [[1 [2]] 3])'              '(1 2 3)'
check '(distinct [1 2 1 3 2])'             '(1 2 3)'
check '(select-keys {:a 1 :b 2 :c 3} [:a :c])' '{:a 1, :c 3}'
check '(sort [3 1 2])'                     '(1 2 3)'
check_multi '(sort-by count ["aaa" "b" "cc"])' '("b" "cc" "aaa")'

# --- ns-publics ---
check '(list? (ns-publics (quote beer.core)))' 'true'
check '(contains? (reduce (fn [m s] (assoc m s true)) {} (ns-publics (quote beer.core))) (quote map))' 'true'

# --- read-string ---
check '(read-string "42")'                  '42'
check '(read-string ":foo")'                ':foo'
check '(read-string "[1 2 3]")'             '[1 2 3]'
check '(list? (read-string "(+ 1 2)"))'     'true'
check '(first (read-string "(+ 1 2)"))'     '+'
check '(read-string "{:a 1}")'              '{:a 1}'

# --- disasm / asm ---
check '(do (defn inc2 [x] (+ x 2)) ((asm (disasm inc2)) 5))' '7'
check '(do (defn f [x] x) (map? (disasm f)))' 'true'
check '(do (defn f [x] x) (:arity (disasm f)))' '1'
check '(do (def f (fn [x] (if x 1 2))) ((asm (disasm f)) true))' '1'
check '(do (def f (fn [x] (if x 1 2))) ((asm (disasm f)) false))' '2'
check '((asm {:code [[:ENTER 1] [:LOAD_LOCAL 0] [:PUSH_INT 1] [:ADD] [:RETURN]] :constants [] :arity 1}) 5)' '6'

# --- doseq ---
check '(do (doseq [x [1 2 3]] x) nil)' 'nil'

# --- qualified macros ---
# (tested via check_script below since require needs BEER_LIB_PATH)

# --- beer.test framework ---
# Test with a failing assertion
check_script() {
    local script="$1" expected="$2" label="$3"
    local tmpfile=$(mktemp /tmp/beer_smoke_XXXXXX.beer)
    printf '%s\n' "$script" > "$tmpfile"
    actual=$(BEERPATH=lib "$BEER" "$tmpfile" 2>/dev/null | tail -1)
    rm -f "$tmpfile"
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: $label => '$actual' (expected '$expected')"
        FAIL=$((FAIL + 1))
    fi
}

check_script '(require (quote beer.test) :as (quote t))
(t/deftest math (t/is (= 2 (+ 1 1))) (t/is (= 6 (* 2 3))))
(t/deftest strs (t/is (= "ab" (str "a" "b"))))
(t/run-tests)' '3 passed, 0 failed.' 'beer.test: passing tests'

check_script '(require (quote beer.test) :as (quote t))
(t/deftest fail-test (t/is (= 1 2)))
(t/run-tests)' '0 passed, 1 failed.' 'beer.test: failing test'

check_script '(require (quote beer.test) :as (quote t))
(t/deftest qualified-macro (t/is true) (t/is (= 42 42)))
(t/run-tests)' '2 passed, 0 failed.' 'qualified macros via beer.test'

# --- circular require detection ---
check_script_err() {
    local script="$1" expected="$2" label="$3"
    local tmpfile=$(mktemp /tmp/beer_smoke_XXXXXX.beer)
    printf '%s\n' "$script" > "$tmpfile"
    # Create circular test libs
    local tmpdir=$(mktemp -d /tmp/beer_circ_XXXXXX)
    mkdir -p "$tmpdir/circ"
    printf '(ns circ.a)\n(require (quote circ.b) :as (quote b))\n(def x 1)\n' > "$tmpdir/circ/a.beer"
    printf '(ns circ.b)\n(require (quote circ.a) :as (quote a))\n(def y 2)\n' > "$tmpdir/circ/b.beer"
    actual=$(BEERPATH="$tmpdir" "$BEER" "$tmpfile" 2>&1 >/dev/null | head -1)
    rm -rf "$tmpfile" "$tmpdir"
    if echo "$actual" | grep -q "$expected"; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: $label => '$actual' (expected match '$expected')"
        FAIL=$((FAIL + 1))
    fi
}

check_script_err '(require (quote circ.a) :as (quote a))' 'circular dependency' 'circular require detection'

# --- BEERPATH multi-directory support ---
check_beerpath() {
    local tmpdir1=$(mktemp -d /tmp/beer_bp1_XXXXXX)
    local tmpdir2=$(mktemp -d /tmp/beer_bp2_XXXXXX)
    mkdir -p "$tmpdir1/mylib" "$tmpdir2/otherlib"
    printf '(ns mylib.greet)\n(def hello "from-dir1")\n' > "$tmpdir1/mylib/greet.beer"
    printf '(ns otherlib.util)\n(def val "from-dir2")\n' > "$tmpdir2/otherlib/util.beer"
    local tmpfile=$(mktemp /tmp/beer_smoke_XXXXXX.beer)
    printf '(require (quote mylib.greet) :as (quote g))\n(require (quote otherlib.util) :as (quote u))\n(println (str g/hello "+" u/val))\n' > "$tmpfile"
    actual=$(BEERPATH="$tmpdir1:$tmpdir2:lib" "$BEER" "$tmpfile" 2>/dev/null | tail -1)
    rm -rf "$tmpfile" "$tmpdir1" "$tmpdir2"
    if [ "$actual" = "from-dir1+from-dir2" ]; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: BEERPATH multi-dir => '$actual' (expected 'from-dir1+from-dir2')"
        FAIL=$((FAIL + 1))
    fi
}
check_beerpath

# --- Tar-based library distribution ---
check_tar_require() {
    local tmpdir=$(mktemp -d /tmp/beer_tar_XXXXXX)
    mkdir -p "$tmpdir/tarlib"
    # Create two .beer files
    printf '(ns tarlib.greet)\n(def greeting "hello-from-tar")\n' > "$tmpdir/tarlib/greet.beer"
    printf '(ns tarlib.math)\n(def magic 42)\n' > "$tmpdir/tarlib/math.beer"
    # Bundle into a tar
    (cd "$tmpdir" && COPYFILE_DISABLE=1 tar cf "$tmpdir/tarlib.tar" tarlib/greet.beer tarlib/math.beer)
    # Remove loose files, keep only tar
    rm -rf "$tmpdir/tarlib"
    # Write test script
    local tmpfile=$(mktemp /tmp/beer_smoke_XXXXXX.beer)
    printf '(require (quote tarlib.greet) :as (quote g))\n(require (quote tarlib.math) :as (quote m))\n(println (str g/greeting "+" m/magic))\n' > "$tmpfile"
    actual=$(BEERPATH="$tmpdir:lib" "$BEER" "$tmpfile" 2>/dev/null | tail -1)
    rm -rf "$tmpfile" "$tmpdir"
    if [ "$actual" = "hello-from-tar+42" ]; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: tar require => '$actual' (expected 'hello-from-tar+42')"
        FAIL=$((FAIL + 1))
    fi
}
check_tar_require

# --- beer.tar namespace natives ---
check_tar_natives() {
    local tmpdir=$(mktemp -d /tmp/beer_tarnative_XXXXXX)
    mkdir -p "$tmpdir"
    printf 'file-a contents\n' > "$tmpdir/a.txt"
    printf 'file-b contents\n' > "$tmpdir/b.txt"
    (cd "$tmpdir" && COPYFILE_DISABLE=1 tar cf "$tmpdir/test.tar" a.txt b.txt)
    local tmpfile=$(mktemp /tmp/beer_smoke_XXXXXX.beer)
    printf '(require (quote beer.tar) :as (quote tar))\n(println (count (tar/list "%s/test.tar")))\n(println (tar/read-entry "%s/test.tar" "a.txt"))\n' "$tmpdir" "$tmpdir" > "$tmpfile"
    local output=$(BEERPATH="lib" "$BEER" "$tmpfile" 2>/dev/null)
    rm -rf "$tmpfile" "$tmpdir"
    local line1=$(echo "$output" | sed -n '1p')
    local line2=$(echo "$output" | sed -n '2p')
    if [ "$line1" = "2" ] && [ "$line2" = "file-a contents" ]; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: tar natives => line1='$line1' line2='$line2' (expected '2' and 'file-a contents')"
        FAIL=$((FAIL + 1))
    fi
}
check_tar_natives

# --- Async I/O (reactor) ---
# File read from spawned task (non-blocking path)
echo "async io test data" > /tmp/beer_async_test.txt
check_multi '(await (spawn (fn [] (read-line (open "/tmp/beer_async_test.txt" :read)))))' \
    '"async io test data"' 'async file read from task'

# Multiple tasks reading different files concurrently
echo "file1" > /tmp/beer_async1.txt
echo "file2" > /tmp/beer_async2.txt
check_multi '(def t1 (spawn (fn [] (read-line (open "/tmp/beer_async1.txt" :read)))))
(def t2 (spawn (fn [] (read-line (open "/tmp/beer_async2.txt" :read)))))
(str (await t1) " " (await t2))' \
    '"file1 file2"' 'concurrent file reads from tasks'

# Standalone (no spawn) I/O still works
check_multi '(read-line (open "/tmp/beer_async_test.txt" :read))' \
    '"async io test data"' 'standalone file read (blocking fallback)'

# slurp still works (uses blocking fd, not stream_open)
check_multi '(slurp "/tmp/beer_async_test.txt")' \
    '"async io test data\n"' 'slurp still works'

# Clean up temp files
rm -f /tmp/beer_async_test.txt /tmp/beer_async1.txt /tmp/beer_async2.txt

# --- beer.shell/exec ---
echo ""
echo "--- beer.shell/exec ---"

check_multi '(require (quote beer.shell) :as (quote shell))
(:exit (shell/exec "echo" "hello"))' '0' 'shell/exec exit code'

check_multi '(require (quote beer.shell) :as (quote shell))
(:out (shell/exec "echo" "hello"))' '"hello\n"' 'shell/exec stdout'

check_multi '(require (quote beer.shell) :as (quote shell))
(:exit (shell/exec "false"))' '1' 'shell/exec non-zero exit'

check_multi '(require (quote beer.shell) :as (quote shell))
(:err (shell/exec "sh" "-c" "echo oops >&2"))' '"oops\n"' 'shell/exec stderr'

# --- CLI subcommands (beer new / run / build) ---
echo ""
echo "--- CLI subcommands ---"

check_cli() {
    local label="$1" expected="$2"
    shift 2
    actual=$("$@" 2>/dev/null)
    if echo "$actual" | grep -q "$expected"; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: $label => '$actual' (expected match '$expected')"
        FAIL=$((FAIL + 1))
    fi
}

CLI_TMPDIR=$(mktemp -d /tmp/beer_cli_XXXXXX)

# beer new
check_cli 'beer new creates project' 'Created project: testproj' \
    "$BEER" new "$CLI_TMPDIR/testproj"

# Verify beer.edn was created
if [ -f "$CLI_TMPDIR/testproj/beer.edn" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: beer.edn not created"
    FAIL=$((FAIL + 1))
fi

# Verify core.beer was created
if [ -f "$CLI_TMPDIR/testproj/src/testproj/core.beer" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: src/testproj/core.beer not created"
    FAIL=$((FAIL + 1))
fi

# beer run
BEER_ABS=$(cd "$(dirname "$BEER")" && pwd)/$(basename "$BEER")
LIB_ABS=$(cd "$(dirname "$BEER")/.." && pwd)/lib
run_output=$(cd "$CLI_TMPDIR/testproj" && BEERPATH="$LIB_ABS" "$BEER_ABS" run 2>/dev/null)
if [ "$run_output" = "Hello from testproj!" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: beer run => '$run_output' (expected 'Hello from testproj!')"
    FAIL=$((FAIL + 1))
fi

# beer build
build_output=$(cd "$CLI_TMPDIR/testproj" && BEERPATH="$LIB_ABS" "$BEER_ABS" build 2>/dev/null)
if echo "$build_output" | grep -q "Built testproj.tar"; then
    PASS=$((PASS + 1))
else
    echo "FAIL: beer build => '$build_output' (expected 'Built testproj.tar')"
    FAIL=$((FAIL + 1))
fi

# Verify tar was created
if [ -f "$CLI_TMPDIR/testproj/testproj.tar" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: testproj.tar not created"
    FAIL=$((FAIL + 1))
fi

# beer --help
help_output=$("$BEER" --help 2>/dev/null)
if echo "$help_output" | grep -q "beer.tools"; then
    # --help doesn't mention beer.tools, but mentions commands
    :
fi
if echo "$help_output" | grep -q "new <name>"; then
    PASS=$((PASS + 1))
else
    echo "FAIL: beer --help missing 'new <name>'"
    FAIL=$((FAIL + 1))
fi

rm -rf "$CLI_TMPDIR"

echo ""
echo "===================="
echo "Passed: $PASS"
echo "Failed: $FAIL"
echo "Total:  $((PASS + FAIL))"
echo "===================="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo "All smoke tests passed!"
exit 0
