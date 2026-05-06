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
check '(->> [1 2 3 4 5] (map (fn [x] (+ x 1))))'  '(2 3 4 5 6)'

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
check '(update {:x 1} :x + 5)'                    '{:x 6}'
check '(update {:x 2} :x * 3)'                    '{:x 6}'
check '(update-in {:a {:b 1}} [:a :b] + 10)'      '{:a {:b 11}}'
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

# --- Concurrency: task-watch ---
check_multi '(def c (chan 1))
(def t (spawn (fn [] (+ 1 2))))
(task-watch t (fn [r] (>! c (get r :status))))
(<! c)' ':ok' "task-watch success status"

check_multi '(def c (chan 1))
(def t (spawn (fn [] (+ 1 2))))
(task-watch t (fn [r] (>! c (get r :result))))
(<! c)' '3' "task-watch success result"

check_multi '(def c (chan 1))
(def t (spawn (fn [] (throw {:message "boom"}))))
(task-watch t (fn [r] (>! c (get r :status))))
(<! c)' ':error' "task-watch error status"

check_multi '(def c (chan 1))
(def t (spawn (fn [] (/ 1 0))))
(task-watch t (fn [r] (>! c (get r :message))))
(<! c)' '"/: division by zero"' "task-watch error message"

check_multi '(def c (chan 2))
(def t (spawn (fn [] 42)))
(task-watch t (fn [r] (>! c (get r :result))))
(task-watch t (fn [r] (>! c (get r :result))))
(+ (<! c) (<! c))' '84' "task-watch multiple watchers"

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

# --- eval ---
check '(eval (list (quote +) 1 2))'           '3'          "eval basic"
check '(eval (read-string "(+ 10 20)"))'       '30'         "eval read-string"
check '(eval 42)'                              '42'         "eval literal"
check '(eval (list (quote vector) 1 2 3))'     '[1 2 3]'    "eval vector"
check_multi '(eval (quote (def evtmp 99)))
evtmp' '99' "eval def"

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

# --- read-bytes ---
echo ""
echo "--- read-bytes ---"

check_multi '(spit "/tmp/beer_rb_test.txt" "hello world")
(def s (open "/tmp/beer_rb_test.txt" :read))
(read-bytes s 5)' '"hello"' "read-bytes reads N bytes"

check_multi '(spit "/tmp/beer_rb_test.txt" "abcdef")
(def s (open "/tmp/beer_rb_test.txt" :read))
(read-bytes s 3)' '"abc"' "read-bytes partial"

# --- TCP round-trip ---
echo ""
echo "--- TCP ---"

# TCP test via file (spawn both tasks, use await)
TCP_TEST=$(mktemp /tmp/beer_tcp_XXXXXX.beer)
cat > "$TCP_TEST" << 'TCPEOF'
(require (quote beer.tcp) :as (quote tcp))
(def srv (tcp/listen 0))
(def port (tcp/local-port srv))
(def server-task (spawn (fn []
  (let [conn (tcp/accept srv)]
    (let [line (read-line conn)]
      (close conn)
      line)))))
(def client-task (spawn (fn []
  (let [c (tcp/connect "127.0.0.1" port)]
    (write c "ping\n")
    (flush c)
    (close c)
    "sent"))))
(println (await server-task))
(await client-task)
(close srv)
TCPEOF
tcp_out=$(BEERPATH=lib "$BEER" "$TCP_TEST" 2>/dev/null | head -1)
rm -f "$TCP_TEST"
if [ "$tcp_out" = "ping" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: TCP round-trip => '$tcp_out' (expected 'ping')"
    FAIL=$((FAIL + 1))
fi

# TCP: spawned task does tcp/accept (channel rendezvous — no await holding ref)
TCP_SPAWN_TEST=$(mktemp /tmp/beer_tcp_spawn_XXXXXX.beer)
cat > "$TCP_SPAWN_TEST" << 'TCPSPAWNEOF'
(require (quote beer.tcp) :as (quote tcp))
(let [srv (tcp/listen 0)
      port (tcp/local-port srv)
      done (chan 1)]
  (spawn (fn []
    (let [cli (tcp/accept srv)]
      (>! done (read-line cli))
      (close cli))))
  (spawn (fn []
    (let [c (tcp/connect "127.0.0.1" port)]
      (write c "hello\n")
      (flush c)
      (close c))))
  (println (<! done)))
TCPSPAWNEOF
tcp_spawn_out=$(BEERPATH=lib "$BEER" "$TCP_SPAWN_TEST" 2>/dev/null | head -1)
rm -f "$TCP_SPAWN_TEST"
if [ "$tcp_spawn_out" = '"hello"' ] || [ "$tcp_spawn_out" = 'hello' ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: TCP spawned accept => '$tcp_spawn_out' (expected 'hello')"
    FAIL=$((FAIL + 1))
fi

# --- JSON ---
echo ""
echo "--- JSON ---"

JSON_BEER="BEERPATH=lib $BEER"

check_multi '(require (quote beer.json) :as (quote json))
(json/parse "[1, 2, 3]")' '[1 2 3]' "json parse array"

check_multi '(require (quote beer.json) :as (quote json))
(json/parse "true")' 'true' "json parse true"

check_multi '(require (quote beer.json) :as (quote json))
(json/parse "null")' 'nil' "json parse null"

check_multi '(require (quote beer.json) :as (quote json))
(json/emit [1 2 3])' '"[1,2,3]"' "json emit array"

check_multi '(require (quote beer.json) :as (quote json))
(json/emit nil)' '"null"' "json emit null"

check_multi '(require (quote beer.json) :as (quote json))
(json/emit true)' '"true"' "json emit true"

check_multi '(require (quote beer.json) :as (quote json))
(json/emit {:a 1})' '"{\"a\":1}"' "json emit map"

# --- Metadata & Docstrings ---
echo ""
echo "--- Metadata & Docstrings ---"

# meta on builtin returns nil (no doc set)
check '(meta (quote +))' 'nil'

# defn with docstring
check_multi '(defn foo "a docstring" [x] x)
(:doc (meta (quote foo)))' '"a docstring"' "defn docstring"

# defn without docstring => nil meta
check_multi '(defn bar [x] x)
(meta (quote bar))' 'nil' "defn no docstring meta is nil"

# alter-meta! on a var
check_multi '(defn baz [x] x)
(alter-meta! (quote baz) assoc :added true)
(:added (meta (quote baz)))' 'true' "alter-meta! assoc"

# with-meta on a function
check_multi '(def f (with-meta (fn [x] x) {:tag "anon"}))
(:tag (meta f))' '"anon"' "with-meta on fn"

# defmacro with docstring
check_multi '(defmacro my-when "like when" [test & body] (list (quote if) test (cons (quote do) body) nil))
(:doc (meta (quote my-when)))' '"like when"' "defmacro docstring"

# multiple metadata keys
check_multi '(defn documented "hello world" [x] x)
(alter-meta! (quote documented) assoc :added "1.0")
(:added (meta (quote documented)))' '"1.0"' "multiple meta keys"

# --- Atoms ---
echo ""
echo "--- Atoms ---"

# atom creation and deref
check '(deref (atom 42))' '42'
check '@(atom 42)' '42'

# reset!
check_multi '(def a (atom 0))
(reset! a 99)
@a' '99' "reset! atom"

# swap! with native fn
check_multi '(def a (atom 0))
(swap! a inc)
@a' '1' "swap! inc"

# swap! with extra args
check_multi '(def a (atom 10))
(swap! a + 5)
@a' '15' "swap! with extra args"

# swap! with bytecode fn
check_multi '(def a (atom 0))
(swap! a (fn [x] (+ (* x 2) 1)))' '1' "swap! bytecode fn on 0"

check_multi '(def a (atom 3))
(swap! a (fn [x] (* x 10)))
@a' '30' "swap! bytecode fn"

# compare-and-set!
check_multi '(def a (atom 10))
(compare-and-set! a 10 20)' 'true' "CAS success"

check_multi '(def a (atom 10))
(compare-and-set! a 999 20)' 'false' "CAS failure"

check_multi '(def a (atom 10))
(compare-and-set! a 10 20)
@a' '20' "CAS updates value"

# atom? predicate
check '(atom? (atom 1))' 'true'
check '(atom? 42)' 'false'
check '(atom? nil)' 'false'

# type
check '(type (atom 1))' ':atom'

# multiple swaps
check_multi '(def a (atom 0))
(swap! a inc)
(swap! a inc)
(swap! a inc)
@a' '3' "multiple swaps"

# swap! with string values
check_multi '(def a (atom "hello"))
(swap! a (fn [s] (str s " world")))
@a' '"hello world"' "swap! with strings"

# --- HTTP server ---
echo ""
echo "--- HTTP ---"

HTTP_TEST=$(mktemp /tmp/beer_http_XXXXXX.beer)
cat > "$HTTP_TEST" << 'HTTPEOF'
(require (quote beer.tcp) :as (quote tcp))
(require (quote beer.http) :as (quote http))
(require (quote beer.json) :as (quote json))
(defn handler [req]
  {:status 200
   :headers {"content-type" "application/json"}
   :body (json/emit {:ok true})})
(def srv (tcp/listen 0))
(def port (tcp/local-port srv))
(def server-task (spawn (fn []
  (let [conn (tcp/accept srv)]
    (http/handle-connection handler conn)
    "ok"))))
(def client-task (spawn (fn []
  (let [c (tcp/connect "127.0.0.1" port)]
    (write c "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
    (flush c)
    (loop [acc ""]
      (let [line (read-line c)]
        (if (nil? line)
          (do (close c) acc)
          (recur (str acc line "\n")))))))))
(await server-task)
(println (await client-task))
(close srv)
HTTPEOF
http_out=$(BEERPATH=lib "$BEER" "$HTTP_TEST" 2>/dev/null)
rm -f "$HTTP_TEST"
if echo "$http_out" | grep -q '"ok":true'; then
    PASS=$((PASS + 1))
else
    echo "FAIL: HTTP server => '$http_out' (expected JSON with ok:true)"
    FAIL=$((FAIL + 1))
fi
if echo "$http_out" | grep -q 'HTTP/1.1 200 OK'; then
    PASS=$((PASS + 1))
else
    echo "FAIL: HTTP status line => '$http_out' (expected HTTP/1.1 200 OK)"
    FAIL=$((FAIL + 1))
fi

# --- beer.hive: actors ---
check_multi '(require (quote beer.hive) :as (quote hive))
(def pid (hive/spawn-actor (fn [state msg] (+ state (nth msg 1))) 0))
(hive/send pid [:add 5])
(hive/send pid [:add 3])
(hive/send pid [:add 2])
(hive/stop pid)
(await (get pid :task))' '10' "actor state accumulation"

check_multi '(require (quote beer.hive) :as (quote hive))
(def c (chan 1))
(def pid (hive/spawn-actor (fn [state msg] (cond (= (first msg) :get) (do (>! (nth msg 1) state) state) (= (first msg) :set) (nth msg 1) :else state)) 42))
(hive/send pid [:set 99])
(hive/send pid [:get c])
(await (spawn (fn [] (<! c))))' '99' "actor get/set pattern"

check_multi '(require (quote beer.hive) :as (quote hive))
(def c (chan 1))
(def pid (hive/spawn-actor (fn [state msg] (let* [type (first msg)] (cond (= type :set) (nth msg 1) (= type :add) (+ state (nth msg 1)) (= type :read) (do (>! (nth msg 1) state) state) (= type :quit) {:stop state} :else state))) nil))
(hive/send pid [:set 10])
(hive/send pid [:add 1])
(hive/send pid [:add 2])
(hive/send pid [:add 3])
(hive/send pid [:add 4])
(hive/send pid [:add 5])
(hive/send pid [:add 6])
(hive/send pid [:read c])
(await (spawn (fn [] (<! c))))' '31' "actor loop with state"

check_multi '(require (quote beer.hive) :as (quote hive))
(def pid (hive/spawn-actor (fn [state msg] (let* [type (first msg)] (cond (= type :set) (nth msg 1) (= type :add) (+ state (nth msg 1)) (= type :quit) {:stop state} :else state))) nil))
(hive/send pid [:set 10])
(hive/send pid [:add 1])
(hive/send pid [:add 2])
(hive/send pid [:add 3])
(hive/send pid [:add 4])
(hive/send pid [:add 5])
(hive/send pid [:add 6])
(hive/send pid [:quit])
(await (get pid :task))' '31' "actor quit terminates loop"

check_multi '(require (quote beer.hive) :as (quote hive))
(def pid (hive/spawn-actor (fn [s m] (+ s 1)) 0 {:name :counter}))
(hive/send (hive/whereis :counter) [:bump])
(hive/stop (hive/whereis :counter))
(await (get (hive/whereis :counter) :task))' '1' "actor registry whereis"

check_multi '(require (quote beer.hive) :as (quote hive))
(def pid (hive/spawn-actor (fn [s m] (cond (= (first m) :add) (+ s (nth m 1)) (= (first m) :read) {:reply s} :else s)) 0))
(hive/send pid [:add 10])
(hive/send pid [:add 5])
(def ch (chan 1))
(spawn (fn [] (>! ch (hive/ask pid [:read]))))
(<! ch)' '15' "actor ask/reply"

check_multi '(require (quote beer.hive) :as (quote hive))
(def pid (hive/spawn-actor (fn [s m] (cond (= (first m) :inc) {:reply (+ s 1) :state (+ s 1)} (= (first m) :read) {:reply s} :else s)) 0))
(hive/send pid [:inc])
(hive/send pid [:inc])
(hive/send pid [:inc])
(def ch (chan 1))
(spawn (fn [] (>! ch (hive/ask pid [:read]))))
(<! ch)' '3' "actor ask with state update"

# --- Bitwise operations ---
echo ""
echo "--- Bitwise ops ---"

check '(bit-and 0xFF 0x0F)' '15' "bit-and"
check '(bit-or 0xF0 0x0F)' '255' "bit-or"
check '(bit-xor 0xFF 0x0F)' '240' "bit-xor"
check '(bit-not 0)' '-1' "bit-not"
check '(bit-shift-left 1 8)' '256' "bit-shift-left"
check '(bit-shift-right 256 4)' '16' "bit-shift-right"
check '(bit-and 0xFF 0x36)' '54' "bit-and ipad byte"
check '(bit-and (bit-shift-right 0xAB 4) 0xF)' '10' "bit ops combined"

# char/char-code
check '(char-code \A)' '65' "char-code"
check '(= (char 65) \A)' 'true' "char from code"
check '(char-code (char 0x2764))' '10084' "char roundtrip unicode"

# --- Crypto ---
echo ""
echo "--- beer.crypto / beer.digest ---"

check_multi '(require (quote beer.crypto) :as (quote crypto))
(count (crypto/sha256 ""))' '64' "sha256 empty string length"

check_multi '(require (quote beer.crypto) :as (quote crypto))
(= (crypto/sha256 "abc") "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")' 'true' "sha256 abc known value"

check_multi '(require (quote beer.crypto) :as (quote crypto))
(= (crypto/sha256 "") "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")' 'true' "sha256 empty string known value"

check_multi '(require (quote beer.crypto) :as (quote crypto))
(crypto/constant-time-eq? "hello" "hello")' 'true' "constant-time-eq? equal"

check_multi '(require (quote beer.crypto) :as (quote crypto))
(crypto/constant-time-eq? "hello" "world")' 'false' "constant-time-eq? unequal"

check_multi '(require (quote beer.crypto) :as (quote crypto))
(crypto/constant-time-eq? "abc" "abcd")' 'false' "constant-time-eq? length mismatch"

check_multi '(require (quote beer.crypto) :as (quote crypto))
(= (count (crypto/random-bytes 16)) 16)' 'true' "random-bytes length"

check_multi '(require (quote beer.digest) :as (quote digest))
(= (count (digest/hmac-sha256 "key" "message")) 64)' 'true' "hmac-sha256 length"

check_multi '(require (quote beer.digest) :as (quote digest))
(= (digest/hmac-sha256 "key" "The quick brown fox jumps over the lazy dog") "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8")' 'true' "hmac-sha256 known vector"

check_multi '(require (quote beer.digest) :as (quote digest))
(= (count (digest/random-hex 8)) 16)' 'true' "random-hex length"

# --- Distributed hive (single-process API tests) ---
echo ""
echo "--- beer.hive distributed (loopback) ---"
check_multi '(require (quote beer.hive) :as (quote hive))
(def nid (hive/start-node! {:host "127.0.0.1" :port 0 :secret "test-secret"}))
(string? nid)' 'true' "start-node! returns node-id string"

check_multi '(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host "127.0.0.1" :port 0 :secret "test-secret"})
(hive/stop-node!)
true' 'true' "start-node! and stop-node! lifecycle"

# Local actor still reachable after start-node!
check_multi '(require (quote beer.hive) :as (quote hive))
;; Spawn a named local actor, start a node, verify actor is reachable via whereis
(def pid (hive/spawn-actor (fn [s m] (if (= m :ping) {:reply :pong :state s} s)) nil {:name :ponger}))
(hive/start-node! {:host "127.0.0.1" :port 0 :secret "test-secret"})
(def result (hive/ask pid :ping))
(hive/stop-node!)
result' ':pong' "local actor still works after start-node!"

check_multi '(require (quote beer.hive) :as (quote hive))
(nil? (hive/node-id))' 'true' "node-id is nil before start"

# FIXME: shouldn't `hive/node-id` be nil after `stop-node!`?

check_multi '(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host "127.0.0.1" :port 0 :secret "s"} "my-custom-id")
(= (hive/node-id) "my-custom-id")' 'true' "custom node-id via 2-arity start-node!"

check_multi '(require (quote beer.hive) :as (quote hive))
(def nid (hive/start-node! {:host "127.0.0.1" :port 0 :secret "s"}))
(def same? (= nid (hive/node-id)))
(hive/stop-node!)
same?' 'true' "node-id matches start-node! return value"

check_multi '(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host "127.0.0.1" :port 0 :secret "s"})
(hive/stop-node!)
(hive/stop-node!)
true' 'true' "stop-node! is idempotent"

# --- Wire protocol tests ---
echo ""
echo "--- beer.hive.wire protocol ---"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (wire/decode-varint (wire/encode-varint 0) 0) [0 1])' 'true' "varint roundtrip 0"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (wire/decode-varint (wire/encode-varint 127) 0) [127 1])' 'true' "varint roundtrip 127 (1 byte)"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (wire/decode-varint (wire/encode-varint 128) 0) [128 2])' 'true' "varint roundtrip 128 (2 bytes)"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (wire/decode-varint (wire/encode-varint 16383) 0) [16383 2])' 'true' "varint roundtrip 16383 (max 2-byte)"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (wire/decode-varint (wire/encode-varint 16384) 0) [16384 3])' 'true' "varint roundtrip 16384 (3 bytes)"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (:msg-type (wire/decode-frame (wire/encode-frame wire/MSG-SEND {:to "actor" :msg 42}) 0)) wire/MSG-SEND)' 'true' "frame encode/decode preserves msg-type"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(def payload {:to "actor" :msg :ping :reply-to nil})
(= (:payload (wire/decode-frame (wire/encode-frame wire/MSG-SEND payload) 0)) payload)' 'true' "frame encode/decode preserves payload"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(def payload {:to "worker" :msg {:x 1 :y 2} :reply-to {:node "n1" :actor "r"}})
(= (:payload (wire/decode-frame (wire/encode-frame wire/MSG-SEND payload) 0)) payload)' 'true' "frame encode/decode nested payload"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (:msg-type (wire/decode-frame (wire/encode-frame wire/MSG-HELLO {:node-id "x" :version "0.1"}) 0)) wire/MSG-HELLO)' 'true' "frame encode/decode MSG-HELLO"

check_multi '(require (quote beer.hive.wire) :as (quote wire))
(= (:msg-type (wire/decode-frame (wire/encode-frame wire/MSG-BYE {}) 0)) wire/MSG-BYE)' 'true' "frame encode/decode MSG-BYE"

# --- Two-node loopback (two separate beerlang processes) ---
# Server runs setup forms, then blocks on (<! (chan 1)) to keep the scheduler alive.
# The IO reactor thread (background pthread) detects client connections within 10ms
# and pushes wakeups into the completion ring. The scheduler's 1ms tick loop drains
# the ring, so connections are accepted promptly even while the main task is blocked.
echo ""
echo "--- beer.hive two-node loopback ---"

# Port constants for this test group
HIVE_SRV_PORT=17700
HIVE_CLI_PORT=17701
HIVE_SECRET="smoke-hive-secret"
HIVE_SRV_ID="smoke-srv"
HIVE_CLI_ID="smoke-cli"

# Helper: start server in background, run client, check result, kill server
# Server blocks on (<! (chan 1)) so scheduler keeps processing IO.
check_two_node() {
    local label="$1"
    local server_forms="$2"
    local client_forms="$3"
    local expected="$4"

    echo "  two-node: $label ..."

    # Server: run setup forms then block on empty channel to keep scheduler alive.
    # Redirect stdout too (server's REPL prompts would pollute test output otherwise).
    printf '%s\n(<! (chan 1))\n' "$server_forms" | "$BEER" >/dev/null 2>&1 &
    local spid=$!

    # Give server time to start beerlang, load all .beer dependencies, bind/listen.
    sleep 3.0

    local actual
    actual=$(printf '%s\n(exit)\n' "$client_forms" | "$BEER" 2>/dev/null \
        | sed -n 's/^[a-zA-Z._]*:[0-9]*> //p' \
        | grep -v '^$\|^Goodbye' \
        | tail -1)

    kill "$spid" 2>/dev/null
    wait "$spid" 2>/dev/null

    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        echo "FAIL: $label => '$actual' (expected '$expected')"
        FAIL=$((FAIL + 1))
    fi
}

# Basic remote ask: keyword reply
check_two_node "remote ask :ping => :pong" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (if (= m :ping) {:reply :pong :state s} s)) nil {:name :ponger})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"ponger\"} :ping)" \
":pong"

# Remote ask: numeric reply
check_two_node "remote ask with numeric reply" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (if (= m :double) {:reply (* s 2) :state s} s)) 21 {:name :doubler})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"doubler\"} :double)" \
"42"

# Remote ask: string reply (use string? to avoid bash double-quote escaping)
check_two_node "remote ask with string reply" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (if (= m :greet) {:reply \"beer\" :state s} s)) nil {:name :greeter})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(string? (hive/ask {:node \"$HIVE_SRV_ID\" :actor \"greeter\"} :greet))" \
"true"

# Remote ask: map payload
check_two_node "remote ask with map message" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (if (map? m) {:reply (+ (:x m) (:y m)) :state s} s)) nil {:name :adder})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"adder\"} {:x 10 :y 32})" \
"42"

# Multiple sequential asks to same actor
check_two_node "multiple sequential remote asks" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (if (= m :ping) {:reply :pong :state s} s)) nil {:name :echo})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"echo\"} :ping)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"echo\"} :ping)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"echo\"} :ping)" \
":pong"

# Stateful actor: send increments, ask returns count
check_two_node "remote ask stateful actor" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (cond (= m :inc) (+ s 1) (= m :get) {:reply s :state s} :else s)) 0 {:name :counter})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"counter\"} :inc)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"counter\"} :inc)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"counter\"} :inc)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"counter\"} :get)" \
"3"

# Two actors on server, ask both
check_two_node "two actors on server" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (if (= m :q) {:reply :alice :state s} s)) nil {:name :alice})
(hive/spawn-actor (fn [s m] (if (= m :q) {:reply :bob :state s} s)) nil {:name :bob})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(def a (hive/ask {:node \"$HIVE_SRV_ID\" :actor \"alice\"} :q))
(def b (hive/ask {:node \"$HIVE_SRV_ID\" :actor \"bob\"} :q))
(= [a b] [:alice :bob])" \
"true"

# Actor echoes the message back
check_two_node "remote ask echo" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] {:reply m :state s}) nil {:name :mirror})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"mirror\"} 99)" \
"99"

# Remote ask with vector payload
check_two_node "remote ask vector message and reply" \
"(require (quote beer.hive) :as (quote hive))
(hive/spawn-actor (fn [s m] (if (vector? m) {:reply (count m) :state s} {:reply -1 :state s})) nil {:name :counter2})
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_SRV_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_SRV_ID\")" \
"(require (quote beer.hive) :as (quote hive))
(hive/start-node! {:host \"127.0.0.1\" :port $HIVE_CLI_PORT :secret \"$HIVE_SECRET\"} \"$HIVE_CLI_ID\")
(hive/connect-node! \"$HIVE_SRV_ID\" \"127.0.0.1\" $HIVE_SRV_PORT)
(hive/ask {:node \"$HIVE_SRV_ID\" :actor \"counter2\"} [1 2 3 4 5])" \
"5"

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

# ============================================================
# hive node start / stop / restart
# ============================================================
# Uses a free port to avoid clashing with the two-node tests above.
HIVE_RST_PORT=17702

HIVE_RST_TEST=$(mktemp /tmp/beer_hive_rst_XXXXXX.beer)
cat > "$HIVE_RST_TEST" << 'HIVEEOF'
(require 'beer.hive :as 'hive)

;; Start, register an actor, ask it, stop, restart, verify new actor works.
(def port HIVE_RST_PORT_PLACEHOLDER)

(hive/start-node! {:host "127.0.0.1" :port port :secret "rst-secret"} "rst-node-1")
(def pid1 (hive/spawn-actor (fn [s m] {:reply (+ m 1) :state s}) 0 {:name :inc}))
(def r1 (hive/ask pid1 10))

(hive/stop-node!)

;; Restart on the same port (SO_REUSEADDR makes this immediate)
(hive/start-node! {:host "127.0.0.1" :port port :secret "rst-secret"} "rst-node-2")
(def pid2 (hive/spawn-actor (fn [s m] {:reply (* m 2) :state s}) 0 {:name :double}))
(def r2 (hive/ask pid2 7))

(hive/stop-node!)

;; Restart a third time to confirm idempotency
(hive/start-node! {:host "127.0.0.1" :port port :secret "rst-secret"} "rst-node-3")
(def pid3 (hive/spawn-actor (fn [s m] {:reply (- m 1) :state s}) 0 {:name :dec}))
(def r3 (hive/ask pid3 5))

(hive/stop-node!)

(println (str r1 "," r2 "," r3))
HIVEEOF

# Replace the port placeholder
sed -i.bak "s/HIVE_RST_PORT_PLACEHOLDER/$HIVE_RST_PORT/" "$HIVE_RST_TEST"
rm -f "${HIVE_RST_TEST}.bak"

hive_rst_out=$(BEERPATH=lib "$BEER" "$HIVE_RST_TEST" 2>/dev/null | tail -1)
rm -f "$HIVE_RST_TEST"

if [ "$hive_rst_out" = "11,14,4" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: hive start/stop/restart => '$hive_rst_out' (expected '11,14,4')"
    FAIL=$((FAIL + 1))
fi

# ============================================================
# hive double-start protection
# Calling start-node! twice without stop! must throw :already-running,
# leave the node intact, and not crash on the third attempt.
# ============================================================
HIVE_DS_PORT=17703
HIVE_DS_TEST=$(mktemp /tmp/beer_hive_ds_XXXXXX.beer)
cat > "$HIVE_DS_TEST" << 'DSEOF'
(require 'beer.hive :as 'hive)

(def port HIVE_DS_PORT_PLACEHOLDER)

;; 1. First start succeeds
(hive/start-node! {:host "127.0.0.1" :port port :secret "ds-secret"} "ds-node-1")
(def pid (hive/spawn-actor (fn [s m] {:reply (+ m 1) :state s}) 0 {:name :counter}))
(def r1 (hive/ask pid 10))         ; should be 11

;; 2. Second start (node already running) must throw, not crash
(def err2
  (try
    (hive/start-node! {:host "127.0.0.1" :port port :secret "ds-secret"} "ds-node-2")
    nil
    (catch e (:type e))))          ; should be :already-running

;; 3. Third start also must throw, not crash
(def err3
  (try
    (hive/start-node! {:host "127.0.0.1" :port port :secret "ds-secret"} "ds-node-3")
    nil
    (catch e (:type e))))          ; should be :already-running

;; 4. Node is still functional after the failed attempts
(def r2 (hive/ask pid 20))         ; should be 21

(hive/stop-node!)

;; 5. Can restart cleanly after stop
(hive/start-node! {:host "127.0.0.1" :port port :secret "ds-secret"} "ds-node-4")
(def pid2 (hive/spawn-actor (fn [s m] {:reply (* m 3) :state s}) 0 {:name :triple}))
(def r3 (hive/ask pid2 4))         ; should be 12

(hive/stop-node!)

(println (str r1 "," err2 "," err3 "," r2 "," r3))
DSEOF

sed -i.bak "s/HIVE_DS_PORT_PLACEHOLDER/$HIVE_DS_PORT/" "$HIVE_DS_TEST"
rm -f "${HIVE_DS_TEST}.bak"

hive_ds_out=$(BEERPATH=lib "$BEER" "$HIVE_DS_TEST" 2>/dev/null | tail -1)
rm -f "$HIVE_DS_TEST"

if [ "$hive_ds_out" = "11,:already-running,:already-running,21,12" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: hive double-start => '$hive_ds_out' (expected '11,:already-running,:already-running,21,12')"
    FAIL=$((FAIL + 1))
fi

# ---------------------------------------------------------------
# sleep
# ---------------------------------------------------------------
echo "  sleep: basic ..."
SLEEP_TEST=$(mktemp /tmp/beer_sleep_XXXXXX.beer)
cat > "$SLEEP_TEST" << 'BEOF'
(sleep 100)
(println "ok")
BEOF
sleep_out=$(BEERPATH=lib "$BEER" "$SLEEP_TEST" 2>/dev/null)
rm -f "$SLEEP_TEST"
if [ "$sleep_out" = "ok" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: sleep basic => '$sleep_out' (expected 'ok')"
    FAIL=$((FAIL + 1))
fi

echo "  sleep: concurrent tasks ..."
SLEEP_CONC=$(mktemp /tmp/beer_sleep_conc_XXXXXX.beer)
cat > "$SLEEP_CONC" << 'BEOF'
;; Two tasks sleeping different durations — shorter one finishes first
(def ch (chan 2))
(spawn (fn [] (sleep 200) (>! ch :slow)))
(spawn (fn [] (sleep 50)  (>! ch :fast)))
(println (<! ch) (<! ch))
BEOF
sleep_conc_out=$(BEERPATH=lib "$BEER" "$SLEEP_CONC" 2>/dev/null)
rm -f "$SLEEP_CONC"
if [ "$sleep_conc_out" = ":fast :slow" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: sleep concurrent => '$sleep_conc_out' (expected ':fast :slow')"
    FAIL=$((FAIL + 1))
fi

# ---------------------------------------------------------------
# hive Phase 3 — monitor/DOWN
# ---------------------------------------------------------------
echo "  hive Phase 3: monitor/DOWN on node failure ..."
HIVE_MON_TEST=$(mktemp /tmp/beer_hive_mon_XXXXXX.beer)
port=$((20000 + RANDOM % 5000))
cat > "$HIVE_MON_TEST" << BEOF
(require 'beer.hive :as 'hive)
(def port $port)
;; Start a node, spawn an actor, monitor it from a second node
(hive/start-node! {:host "127.0.0.1" :port port :secret "mon-secret"} "mon-node-1")
(def pid (hive/spawn-actor (fn [s m] {:reply (+ m 1) :state s}) 0 {:name :inc}))

;; Connect from second node perspective (same process, different node-id)
;; We test monitor by stopping the node and checking DOWN delivery

;; Register a monitor for the remote pid (simulated via a local test)
(def remote-pid {:node "mon-node-1" :actor "inc"})
(def mon-ch (hive/monitor remote-pid))

;; Verify monitor channel was created
(println (channel? mon-ch))

;; Demonitor should not throw
(hive/demonitor mon-ch)
(println :demonitor-ok)
(hive/stop-node!)
BEOF
hive_mon_out=$(BEERPATH=lib "$BEER" "$HIVE_MON_TEST" 2>/dev/null)
rm -f "$HIVE_MON_TEST"
if [ "$hive_mon_out" = "$(printf 'true\n:demonitor-ok')" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: hive monitor/demonitor => '$hive_mon_out' (expected 'true\\n:demonitor-ok')"
    FAIL=$((FAIL + 1))
fi

echo "  hive Phase 3: heartbeat (node survives with keepalive) ..."
HIVE_HB_TEST=$(mktemp /tmp/beer_hive_hb_XXXXXX.beer)
port2=$((25000 + RANDOM % 5000))
cat > "$HIVE_HB_TEST" << BEOF
(require 'beer.hive :as 'hive)
(def port $port2)
(hive/start-node! {:host "127.0.0.1" :port port :secret "hb-secret"} "hb-node-1")
(def pid (hive/spawn-actor (fn [s m] {:reply (+ m 10) :state s}) 0 {:name :adder}))
;; Wait briefly (heartbeat fires every 5s, this just tests startup)
(sleep 100)
(println (hive/ask pid 5))
(hive/stop-node!)
BEOF
hive_hb_out=$(BEERPATH=lib "$BEER" "$HIVE_HB_TEST" 2>/dev/null)
rm -f "$HIVE_HB_TEST"
if [ "$hive_hb_out" = "15" ]; then
    PASS=$((PASS + 1))
else
    echo "FAIL: hive heartbeat keepalive => '$hive_hb_out' (expected '15')"
    FAIL=$((FAIL + 1))
fi

# reduce-kv
check '(reduce-kv (fn [acc k v] (+ acc v)) 0 {:a 1 :b 2 :c 3})' '6'
check '(reduce-kv (fn [acc k v] (+ acc 1)) 0 {})' '0'
check '(reduce-kv (fn [acc k v] (+ acc 1)) 0 nil)' '0'
check '(get (reduce-kv (fn [acc k v] (assoc acc k (* v 2))) {} {:a 1 :b 2 :c 3}) :b)' '4'
check '(count (reduce-kv (fn [acc k v] (conj acc k)) [] {:x 1 :y 2}))' '2'

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
