# Beerlang Emacs Mode

Emacs support for [Beerlang](https://github.com/nvlass/beerlang) — a
Clojure-flavoured LISP with cooperative multitasking.

## Files

| File | Purpose |
|------|---------|
| `beerlang-mode.el` | Major mode: syntax highlighting, indentation, sexp navigation |
| `beerlang-repl.el` | Comint-based local process REPL |
| `beerlang.el` | Package entry point — loads both layers, adds keybindings |

**Coming:** `beerlang-nrepl.el` — network REPL client (structured eval / doc /
completion over TCP once `beer.nrepl` server exists).

## Installation

```elisp
(add-to-list 'load-path "/path/to/beerlang/editors/emacs")
(require 'beerlang)

;; Point at your binary and library path
(setq beerlang-repl-program  "/path/to/bin/beerlang"
      beerlang-repl-beerpath "/path/to/beerlang/lib")
```

`.beer` files are associated with `beerlang-mode` automatically.

### use-package

```elisp
(use-package beerlang
  :load-path "~/src/beerlang/editors/emacs"
  :custom
  (beerlang-repl-program  "~/src/beerlang/bin/beerlang")
  (beerlang-repl-beerpath "~/src/beerlang/lib"))
```

## Key Bindings

### Source buffers (`beerlang-mode`)

| Key | Command |
|-----|---------|
| `C-c C-z` | Switch to REPL (start if needed) |
| `C-x C-e` / `C-c C-e` | Eval sexp before point |
| `C-c C-c` | Eval top-level form (defn / def / …) |
| `C-c C-r` | Eval region |
| `C-c C-b` | Eval entire buffer |
| `C-c C-l` | Load file into REPL |
| `C-c C-n` | Set REPL namespace to match current file's `(ns ...)` |

### REPL buffer (`beerlang-repl-mode`)

All standard `comint-mode` bindings apply (`M-p`/`M-n` history, etc.).

| Key | Command |
|-----|---------|
| `C-c C-l` | Load a file |
| `C-c C-n` | Switch namespace |

## Features

- **Syntax highlighting** — special forms, macros, built-ins, keywords,
  character literals, numeric literals (including hex and ratios)
- **Indentation** — standard Lisp indentation engine with beerlang-specific
  rules for all special forms and macros; fully compatible with paredit,
  lispy, and smartparens
- **sexp navigation** — `C-M-f/b/u/d` work correctly with `[...]` and `{...}`
- **imenu** — jump to `defn`, `defmacro`, `def`, `defmulti`
- **which-function-mode** — shows current definition in modeline
- **project integration** — `M-x beerlang-run-project` / `beerlang-run-tests`
  via `beer run` / `beer test`

## Roadmap

1. **`beerlang-nrepl.el`** — TCP client for structured evaluation once
   `beer.nrepl` server is implemented
2. **Eldoc** — show arity and docstring in echo area via nREPL `:doc` op
3. **Flycheck checker** — inline linter annotations via nREPL `:lint` op
4. **Company/Cape completion** — symbol completion from the running REPL
