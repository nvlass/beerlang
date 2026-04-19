;;; beerlang-mode.el --- Major mode for the Beerlang language -*- lexical-binding: t; -*-

;; Copyright (C) 2025 Beerlang Contributors
;; Keywords: languages lisp
;; Version: 0.1.0
;; Package-Requires: ((emacs "27.1"))

;;; Commentary:
;; Syntax highlighting, indentation, and sexp navigation for Beerlang
;; (.beer files).  Piggybacks on Emacs' built-in Lisp indentation engine
;; so paredit, lispy, and electric-pair all work out of the box.

;;; Code:

(require 'lisp-mode)

;; ================================================================
;; Customisation
;; ================================================================

(defgroup beerlang nil
  "Beerlang language support."
  :prefix "beerlang-"
  :group 'languages
  :link '(url-link "https://github.com/nvlass/beerlang"))

;; ================================================================
;; Syntax table
;; ================================================================

(defvar beerlang-mode-syntax-table
  (let ((st (make-syntax-table)))
    ;; Comments: ; through end of line
    (modify-syntax-entry ?\; "<"  st)
    (modify-syntax-entry ?\n ">"  st)
    ;; Strings
    (modify-syntax-entry ?\" "\"" st)
    ;; Paired delimiters
    (modify-syntax-entry ?\( "()" st)
    (modify-syntax-entry ?\) ")(" st)
    (modify-syntax-entry ?\[ "(]" st)
    (modify-syntax-entry ?\] ")[" st)
    (modify-syntax-entry ?\{ "(}" st)
    (modify-syntax-entry ?\} "){" st)
    ;; Escape character (for \newline etc.)
    (modify-syntax-entry ?\\ "\\" st)
    ;; Prefix / dispatch characters (treated as part of the next token)
    (modify-syntax-entry ?# "'"   st)
    (modify-syntax-entry ?@ "'"   st)
    (modify-syntax-entry ?' "'"   st)
    (modify-syntax-entry ?` "'"   st)
    (modify-syntax-entry ?, "'"   st)
    (modify-syntax-entry ?^ "'"   st)
    ;; Symbol constituents (Clojure-style identifiers)
    (dolist (ch '(?- ?_ ?/ ?? ?! ?+ ?* ?< ?> ?= ?& ?. ?: ?%))
      (modify-syntax-entry ch "_" st))
    st)
  "Syntax table for `beerlang-mode'.")

;; ================================================================
;; Font-lock keywords
;; ================================================================

(eval-and-compile
  (defun beerlang--sym-rx (words)
    "Return a regexp matching any symbol in the list WORDS as a whole symbol."
    (concat "\\_<" (regexp-opt words t) "\\_>")))

(defconst beerlang--special-forms
  '("def" "if" "do" "let" "let*" "quote" "fn" "loop" "recur"
    "try" "catch" "finally" "throw" "yield" "spawn" "await" "defmacro"
    "and" "or" "ns")
  "Beerlang special forms — part of the core language.")

(defconst beerlang--definition-macros
  '("defn" "defn-" "defonce" "defmulti" "defmethod")
  "Beerlang definition macros.")

(defconst beerlang--macros
  '("when" "when-not" "cond" "case" "while" "doseq" "with-open"
    "let" "->" "->>" "doc" "require" "use" "in-ns" "refer")
  "Beerlang user-facing macros from core.beer.")

(defconst beerlang--builtins
  '(;; Arithmetic
    "+" "-" "*" "/" "mod" "rem" "quot" "inc" "dec" "max" "min" "abs"
    "zero?" "pos?" "neg?" "even?" "odd?"
    ;; Comparison / equality
    "=" "not=" "<" ">" "<=" ">=" "compare"
    ;; Logic
    "not" "boolean"
    ;; Type predicates
    "nil?" "true?" "false?" "string?" "number?" "integer?" "float?"
    "keyword?" "symbol?" "fn?" "map?" "vector?" "list?" "set?"
    "seq?" "coll?" "atom?" "chan?"
    ;; Constructors
    "list" "vector" "hash-map" "array-map" "sorted-map"
    "hash-set" "sorted-set"
    ;; Sequence operations
    "count" "empty?" "first" "second" "rest" "next" "last" "butlast"
    "cons" "conj" "concat" "into" "seq" "vals" "keys"
    "nth" "get" "get-in" "assoc" "assoc-in" "dissoc" "update" "update-in"
    "merge" "merge-with" "select-keys" "contains?"
    "map" "mapv" "filter" "filterv" "remove" "keep"
    "reduce" "reduce-kv" "every?" "some" "any?" "not-any?" "not-every?"
    "take" "drop" "take-while" "drop-while" "take-last" "drop-last"
    "sort" "sort-by" "reverse" "distinct" "group-by" "frequencies"
    "flatten" "partition" "partition-all" "interleave" "interpose"
    "range" "repeat" "repeatedly" "iterate" "cycle"
    "apply" "partial" "comp" "juxt" "complement" "memoize"
    "identity" "constantly" "fnil"
    ;; Strings
    "str" "subs" "name" "namespace" "keyword" "symbol"
    "pr-str" "print-str" "println-str"
    ;; I/O
    "println" "print" "prn" "pr" "newline" "flush"
    "read-line" "read-string" "read-bytes" "slurp" "spit"
    "open" "close" "write" "write-bytes"
    ;; Atoms & concurrency
    "atom" "deref" "reset!" "swap!" "compare-and-set!"
    "chan" ">!" "<!" "close!" "timeout"
    ;; Metadata
    "meta" "with-meta" "alter-meta!" "vary-meta"
    ;; Introspection / misc
    "type" "hash" "identical?" "gensym"
    "eval" "macroexpand" "macroexpand-1"
    "ex-info" "ex-message" "ex-data"
    "disasm" "asm" "char" "char-code"
    "int" "float")
  "Beerlang built-in functions.")

(defconst beerlang-font-lock-keywords
  `(
    ;; nil / true / false — constants
    (,(beerlang--sym-rx '("nil" "true" "false"))
     . 'font-lock-constant-face)

    ;; Keywords  :foo  :ns/bar  ::local
    ("\\(::\\?[a-zA-Z*+!?.%$<>_/-][a-zA-Z0-9*+!?.%$<>_/-]*\\)"
     . 'font-lock-builtin-face)

    ;; Character literals  \a  \newline  \space  \u03BB
    ("\\(\\\\\\(?:newline\\|space\\|tab\\|return\\|backspace\\|formfeed\\|u[0-9a-fA-F]\\{4\\}\\|.\\)\\)"
     . 'font-lock-string-face)

    ;; (defn name …) and friends — highlight form name + var name
    (,(concat "(\\(?:" (regexp-opt beerlang--definition-macros) "\\)"
              "[ \t]+\\([[:alnum:]_/.*+!?<>=-]+\\)")
     (0 'font-lock-keyword-face)
     (1 'font-lock-function-name-face))

    ;; Special forms
    (,(beerlang--sym-rx beerlang--special-forms)
     . 'font-lock-keyword-face)

    ;; Core macros
    (,(beerlang--sym-rx beerlang--macros)
     . 'font-lock-keyword-face)

    ;; Built-in functions
    (,(beerlang--sym-rx beerlang--builtins)
     . 'font-lock-builtin-face)

    ;; Numeric literals  42  3.14  0xFF  1/2  1e10
    ("\\<\\(0[xX][0-9a-fA-F]+\\|[0-9]+/[0-9]+\\|[0-9]+\\(?:\\.[0-9]*\\)?\\(?:[eE][+-]?[0-9]+\\)?\\)\\>"
     . 'font-lock-constant-face)
    )
  "Font-lock keywords for `beerlang-mode'.")

;; ================================================================
;; Indentation
;; ================================================================
;; We use Emacs' built-in lisp indentation engine and just teach it
;; about beerlang-specific forms via the `lisp-indent-function' property.
;; Integer N → first N args are "distinguished" (body indented 2 from paren).
;; Symbol `defun' → function-definition style.

(defun beerlang--put-indent (sym val)
  "Set indentation rule VAL for SYM."
  (put sym 'lisp-indent-function val))

(defun beerlang--configure-indentation ()
  "Register indentation rules for Beerlang forms."
  ;; Special forms
  (beerlang--put-indent 'def        1)
  (beerlang--put-indent 'if         1)
  (beerlang--put-indent 'do         0)
  (beerlang--put-indent 'let        1)
  (beerlang--put-indent 'let*       1)
  (beerlang--put-indent 'fn         1)
  (beerlang--put-indent 'loop       1)
  (beerlang--put-indent 'try        0)
  (beerlang--put-indent 'catch      2)
  (beerlang--put-indent 'finally    0)
  (beerlang--put-indent 'ns         1)
  (beerlang--put-indent 'spawn      0)
  (beerlang--put-indent 'await      0)
  (beerlang--put-indent 'yield      0)
  (beerlang--put-indent 'defmacro   'defun)
  ;; Macros
  (beerlang--put-indent 'defn       'defun)
  (beerlang--put-indent 'defn-      'defun)
  (beerlang--put-indent 'defonce    'defun)
  (beerlang--put-indent 'defmulti   'defun)
  (beerlang--put-indent 'defmethod  2)
  (beerlang--put-indent 'when       1)
  (beerlang--put-indent 'when-not   1)
  (beerlang--put-indent 'cond       0)
  (beerlang--put-indent 'case       1)
  (beerlang--put-indent 'doseq      1)
  (beerlang--put-indent 'with-open  1)
  (beerlang--put-indent 'require    0))

;; ================================================================
;; Keymap (populated by beerlang.el once repl is loaded)
;; ================================================================

(defvar beerlang-mode-map
  (let ((m (make-sparse-keymap)))
    m)
  "Keymap for `beerlang-mode'.
REPL interaction keys are added by beerlang-repl.el.")

;; ================================================================
;; imenu — jump to top-level definitions
;; ================================================================

(defconst beerlang--imenu-generic-expression
  '(("Functions" "^(defn-?[ \t]+\\(\\(?:\\sw\\|\\s_\\)+\\)" 1)
    ("Macros"    "^(defmacro[ \t]+\\(\\(?:\\sw\\|\\s_\\)+\\)"  1)
    ("Vars"      "^(def[ \t]+\\(\\(?:\\sw\\|\\s_\\)+\\)"       1)
    ("Multis"    "^(defmulti[ \t]+\\(\\(?:\\sw\\|\\s_\\)+\\)"  1))
  "imenu patterns for `beerlang-mode'.")

;; ================================================================
;; Mode definition
;; ================================================================

;;;###autoload
(define-derived-mode beerlang-mode prog-mode "Beer"
  "Major mode for editing Beerlang (.beer) source files.

Beerlang is a Clojure-flavoured LISP with cooperative multitasking,
a cache-efficient VM, and a distributed actor system (beer.hive).

Indentation and sexp navigation are handled by Emacs' standard Lisp
machinery, so paredit / lispy / smartparens work without configuration.

\\{beerlang-mode-map}"
  :syntax-table beerlang-mode-syntax-table

  ;; Comments
  (setq-local comment-start       ";")
  (setq-local comment-start-skip  ";+\\s-*")
  (setq-local comment-end         "")
  (setq-local comment-column      40)
  (setq-local comment-add         1)         ; default to ;; not ;

  ;; Font-lock
  (setq-local font-lock-defaults
              '(beerlang-font-lock-keywords
                nil  ; strings/comments handled by syntax table
                nil  ; not case-insensitive
                nil  ; no special syntax overrides
                nil))
  (setq-local font-lock-multiline t)

  ;; Indentation: delegate to Emacs' lisp-indent-line
  (setq-local indent-line-function  #'lisp-indent-line)
  (setq-local indent-tabs-mode       nil)
  (setq-local tab-width              2)
  (beerlang--configure-indentation)

  ;; Electric pairs
  (setq-local electric-pair-pairs
              '((?\( . ?\)) (?\[ . ?\]) (?\{ . ?\}) (?\" . ?\")))

  ;; imenu
  (setq-local imenu-generic-expression
              beerlang--imenu-generic-expression)
  (imenu-add-menubar-index)

  ;; which-function-mode support
  (setq-local add-log-current-defun-function #'beerlang-current-defun))

;; ================================================================
;; which-function / add-log support
;; ================================================================

(defun beerlang-current-defun ()
  "Return the name of the enclosing top-level definition, or nil."
  (save-excursion
    (condition-case nil
        (progn
          (beginning-of-defun)
          (forward-char)             ; skip opening paren
          (forward-sexp)             ; skip def form (defn, def, …)
          (skip-syntax-forward " ")
          (let ((start (point)))
            (forward-sexp)
            (buffer-substring-no-properties start (point))))
      (error nil))))

;; ================================================================
;; File associations
;; ================================================================

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.beer\\'" . beerlang-mode))

(provide 'beerlang-mode)
;;; beerlang-mode.el ends here
