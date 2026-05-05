# Beerlang Emacs Mode

Emacs support for [Beerlang](https://github.com/nvlass/beerlang) — a
Clojure-flavoured LISP with cooperative multitasking.

## Files

| File | Purpose |
|------|---------|
| `beerlang-mode.el` | Major mode: syntax highlighting, indentation, sexp navigation |
| `beerlang-repl.el` | Comint-based local process REPL **and** network REPL client |
| `beerlang.el` | Package entry point — loads both layers, adds keybindings |

Network REPL support is built into `beerlang-repl.el` — no extra file needed.
See [Network REPL](#network-repl-beernrepl) below.

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
| `C-c C-z` | Switch to active REPL (nREPL if connected, else local) |
| `C-x C-e` / `C-c C-e` | Eval sexp before point |
| `C-c C-c` | Eval top-level form (defn / def / …) |
| `C-c C-r` | Eval region |
| `C-c C-b` | Eval entire buffer |
| `C-c C-l` | Load file into REPL |
| `C-c C-n` | Set REPL namespace to match current file's `(ns ...)` |
| `C-c C-j` | Connect to a network REPL (`beerlang-connect`) |
| `C-c C-q` | Disconnect from network REPL (`beerlang-disconnect`) |

### Local REPL buffer (`beerlang-repl-mode`)

All standard `comint-mode` bindings apply (`M-p`/`M-n` history, etc.).

| Key | Command |
|-----|---------|
| `C-c C-l` | Load a file |
| `C-c C-n` | Switch namespace |

### Network REPL buffer (`beerlang-nrepl-mode`)

Same comint bindings as the local REPL. Prompt: `nrepl> `.

## Features

- **Syntax highlighting** — special forms, macros, built-ins, keywords,
  character literals, numeric literals (including hex and ratios)
- **Indentation** — standard Lisp indentation engine with beerlang-specific
  rules for all special forms and macros; fully compatible with paredit,
  lispy, and smartparens
- **sexp navigation** — `C-M-f/b/u/d` work correctly with `[...]` and `{...}`
- **imenu** — jump to `defn`, `defmacro`, `def`, `defmulti`
- **which-function-mode** — shows current definition in modeline
- **Eldoc** — docstring in echo area via nREPL `:doc` op (when connected)
- **project integration** — `M-x beerlang-run-project` / `beerlang-run-tests`
  via `beer run` / `beer test`

## Network REPL (`beer.nrepl`)

`beerlang-repl.el` includes a built-in TCP client for connecting to a
`beer.nrepl` server running inside any beerlang process — a long-running
script, an HTTP server, or an embedded game.

### Starting the server (in beerlang)

```clojure
(require 'beer.nrepl)
(beer.nrepl/start! 7888)   ; starts listening, returns actual port
```

### Connecting from Emacs

```
M-x beerlang-connect    — prompts for host (default localhost) and port (default 7888)
```

Or from a `.beer` source buffer: `C-c C-j`.

Once connected:
- All eval commands (`C-x C-e`, `C-c C-c`, `C-c C-r`, `C-c C-b`) send to
  the remote process instead of the local subprocess.
- `C-c C-z` switches to `*beerlang-nrepl*`.
- `C-c C-n` sends `(in-ns 'ns)` to the remote process.
- **Eldoc** activates automatically — move point over any symbol to see its
  docstring in the echo area.

Disconnect with `C-c C-q` (`M-x beerlang-disconnect`) to fall back to the
local REPL.

### Protocol

EDN map messages, one per line each direction:

```
Client: {:op "eval" :code "(+ 1 2)" :id "id-1"}
Server: {:id "id-1" :value "3"}
        {:id "id-1" :status "done"}
```

Every request gets a final `{:status "done"}` response. The `:id` field
correlates async responses — eldoc can fire a `:doc` request while an eval
is still in flight.

### Eldoc

When connected, moving point over a symbol sends a `{:op "doc" :sym "..."}`
request and displays the result in the echo area. Works in both `.beer`
source buffers and the `*beerlang-nrepl*` buffer.

### Thread safety

nREPL client tasks run on scheduler worker threads. `swap!` atoms and send on
channels freely. Do not call main-thread-only APIs (graphics, UI) directly from
the REPL; modify state via atoms and let the main loop pick it up next iteration.

## Simple REPL (`beer.nrepl.simple`)

For `nc`/telnet access — no framing, no ops, just eval:

```clojure
(require 'beer.nrepl.simple)
(beer.nrepl.simple/start! 7889)
```

```
$ nc localhost 7889
(+ 1 2)
3
(map inc [1 2 3])
(2 3 4)
```

Both servers can run simultaneously. The common pattern:
- Port 7888 → `beer.nrepl` for Emacs (structured, eldoc, future completion)
- Port 7889 → `beer.nrepl.simple` for quick `nc` access

## Roadmap

1. **Flycheck checker** — inline error annotations (needs a `:lint` op)
2. **Company/Cape completion** — symbol completion from the running REPL
