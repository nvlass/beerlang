;;; beerlang-repl.el --- Beerlang REPL (local process + network) -*- lexical-binding: t; -*-

;; Copyright (C) 2025 Beerlang Contributors
;; Keywords: languages lisp
;; Package-Requires: ((emacs "27.1") (beerlang-mode "0.1.0"))

;;; Commentary:
;; Comint-based REPL integration for Beerlang.
;;
;; Local REPL (subprocess):
;;   M-x beerlang-run-repl        — start or switch to the local REPL buffer
;;
;; Network REPL — structured EDN protocol (beer.nrepl):
;;   M-x beerlang-connect         — connect to host:port (default localhost:7888)
;;   M-x beerlang-disconnect      — close the network connection
;;   Eldoc shows docstrings automatically when connected.
;;
;; Simple REPL — plain eval over TCP (beer.nrepl.simple), for nc/telnet:
;;   (beer.nrepl.simple/start! 7889)
;;   nc localhost 7889 → type forms, get results
;;
;; From a .beer source buffer (after (require 'beerlang)):
;;   C-c C-z  — switch to REPL (nREPL if connected, else local)
;;   C-x C-e  — eval sexp before point
;;   C-c C-c  — eval top-level form (defun) around point
;;   C-c C-r  — eval active region
;;   C-c C-b  — eval entire buffer
;;   C-c C-l  — load file into REPL
;;   C-c C-n  — set REPL namespace to match current file's (ns ...)
;;
;; When a network REPL is connected it takes priority over the local REPL for
;; all eval commands.  Disconnect to fall back to the local process.

;;; Code:

(require 'comint)
(require 'cl-lib)
(eval-when-compile (require 'beerlang-mode))
(require 'beerlang-mode)

;; ================================================================
;; Customisation
;; ================================================================

(defcustom beerlang-repl-program "beerlang"
  "Path to the Beerlang executable used for the REPL.
Can be an absolute path or a name resolved through `exec-path'."
  :type 'string
  :group 'beerlang)

(defcustom beerlang-repl-arguments '()
  "Extra arguments passed to `beerlang-repl-program' on startup."
  :type '(repeat string)
  :group 'beerlang)

(defcustom beerlang-repl-beerpath nil
  "Value of the BEERPATH environment variable for the REPL process.
Nil means inherit from the parent environment."
  :type '(choice (const :tag "Inherit" nil)
                 (string :tag "Path"))
  :group 'beerlang)

(defcustom beerlang-repl-popup t
  "When non-nil, switch to the REPL window after sending code."
  :type 'boolean
  :group 'beerlang)

;; ================================================================
;; Internal state
;; ================================================================

(defconst beerlang-repl--buffer-name "*beerlang-repl*"
  "Name of the Beerlang local REPL buffer.")

(defconst beerlang-repl--prompt-re "^[a-zA-Z._-]*:[0-9]+>\\s-?"
  "Regular expression matching a Beerlang local REPL prompt.")

(defconst beerlang-nrepl--buffer-name "*beerlang-nrepl*"
  "Name of the Beerlang network REPL buffer.")

(defconst beerlang-nrepl--prompt "nrepl> "
  "Prompt string inserted into the nREPL buffer after each response.")

(defconst beerlang-nrepl--prompt-re "^nrepl> "
  "Regexp matching the nREPL prompt (used by comint for history navigation).")

;; Buffer-local state for each nREPL connection
(defvar-local beer-nrepl--pending nil
  "Hash table mapping request ID strings to response callback functions.
Callbacks receive each response plist; nil means display in buffer.")

(defvar-local beer-nrepl--recv-buf ""
  "Accumulator for partial server messages (bytes between newlines.")

;; ================================================================
;; Process management
;; ================================================================

(defun beerlang-repl--process ()
  "Return the Beerlang REPL process, or nil if not running."
  (get-buffer-process beerlang-repl--buffer-name))

(defun beerlang-repl--running-p ()
  "Return non-nil if the Beerlang REPL is live."
  (let ((proc (beerlang-repl--process)))
    (and proc (process-live-p proc))))

(defun beerlang-run-repl ()
  "Start the Beerlang REPL or switch to its buffer.
Respects `beerlang-repl-program', `beerlang-repl-arguments', and
`beerlang-repl-beerpath'."
  (interactive)
  (let* ((buf (get-buffer-create beerlang-repl--buffer-name))
         (alive (beerlang-repl--running-p)))
    (unless alive
      (with-current-buffer buf
        (let ((process-environment
               (if beerlang-repl-beerpath
                   (cons (concat "BEERPATH=" beerlang-repl-beerpath)
                         process-environment)
                 process-environment)))
          (apply #'make-comint-in-buffer
                 "beerlang-repl"
                 buf
                 beerlang-repl-program
                 nil
                 beerlang-repl-arguments))
        (beerlang-repl-mode)
        ;; Let the process print its banner before we start sending
        (accept-process-output (beerlang-repl--process) 1)))
    (pop-to-buffer buf)))

;; ================================================================
;; Network REPL (nREPL) — EDN message protocol
;; ================================================================
;;
;; Wire format: one EDN map per line, both directions.
;;
;; Client → server:
;;   {:op "eval"     :code "(+ 1 2)"  :id "id-1"}
;;   {:op "doc"      :sym  "map"      :id "id-2"}
;;   {:op "describe"                  :id "id-3"}
;;
;; Server → client (one or more messages per request, :status "done" last):
;;   {:id "id-1" :value "3"}
;;   {:id "id-1" :status "done"}
;;
;; A nil :id means the response has no matching request (e.g. server-side
;; stdout) and is always displayed in the buffer.

(defcustom beerlang-nrepl-default-host "localhost"
  "Default host for `beerlang-connect'."
  :type 'string
  :group 'beerlang)

(defcustom beerlang-nrepl-default-port 7888
  "Default port for `beerlang-connect'."
  :type 'integer
  :group 'beerlang)

;; ── Request ID counter ───────────────────────────────────────────────────────

(defvar beer-nrepl--id-counter 0
  "Monotonic counter for unique nREPL request IDs.")

(defun beer-nrepl--next-id ()
  "Return a fresh unique request ID string."
  (format "id-%d" (cl-incf beer-nrepl--id-counter)))

;; ── Connection helpers ───────────────────────────────────────────────────────

(defun beerlang-nrepl--process ()
  "Return the live nREPL process, or nil."
  (get-buffer-process beerlang-nrepl--buffer-name))

(defun beerlang-nrepl--connected-p ()
  "Return non-nil if an nREPL connection is live."
  (let ((p (beerlang-nrepl--process)))
    (and p (process-live-p p))))

;; ── EDN encoding ─────────────────────────────────────────────────────────────
;; We build EDN map strings with format/%S.  %S gives Lisp print syntax for
;; strings, which matches EDN string syntax (same escaping rules).

(defun beer-nrepl--build-msg (op id &rest kvs)
  "Return a wire-ready EDN map string for OP and ID.
KVTS is a flat list of alternating \":key\" \"value\" strings.
String values have text properties stripped before encoding — Emacs
encodes propertized strings as #(\"str\" 0 N (prop val)), which
beerlang's reader would interpret as an anonymous function dispatch."
  (let ((parts (list (format ":op %S :id %S" op id))))
    (while kvs
      (let* ((k (car kvs))
             (v (cadr kvs))
             (v (if (stringp v) (substring-no-properties v) v)))
        (push (format "%s %S" k v) parts))
      (setq kvs (cddr kvs)))
    (concat "{" (mapconcat #'identity (nreverse parts) " ") "}\n")))

;; ── EDN decoding ─────────────────────────────────────────────────────────────
;; Server sends maps like {:id "id-1" :value "3"}.
;; Trick: wrap the { } content in ( ) and use Emacs `read', which gives a
;; list; treat it as a plist with `plist-get'.

(defun beer-nrepl--read-msg (str)
  "Parse an EDN map string STR into a plist, or nil on failure."
  (condition-case nil
      (let ((s (string-trim str)))
        (when (and (string-prefix-p "{" s) (string-suffix-p "}" s))
          (read (concat "(" (substring s 1 (1- (length s))) ")"))))
    (error nil)))

;; ── Process filter ────────────────────────────────────────────────────────────

(defun beer-nrepl--filter (proc string)
  "Accumulate STRING from PROC; dispatch complete EDN lines."
  (when (buffer-live-p (process-buffer proc))
    (with-current-buffer (process-buffer proc)
      (setq beer-nrepl--recv-buf (concat beer-nrepl--recv-buf string))
      (while (string-match "\n" beer-nrepl--recv-buf)
        (let ((line (substring beer-nrepl--recv-buf 0 (match-beginning 0))))
          (setq beer-nrepl--recv-buf
                (substring beer-nrepl--recv-buf (match-end 0)))
          (beer-nrepl--handle-msg proc (beer-nrepl--read-msg line)))))))

(defun beer-nrepl--handle-msg (proc msg)
  "Route a parsed response MSG plist.
Tooling ops with a registered callback call it; everything else displays."
  (when msg
    (let* ((id   (plist-get msg :id))
           (done (let ((st (plist-get msg :status)))
                   (or (equal st "done")
                       (and (listp st) (member "done" st)))))
           (cb   (and id (gethash id beer-nrepl--pending))))
      (if cb
          (progn (funcall cb msg)
                 (when done (remhash id beer-nrepl--pending)))
        (beer-nrepl--display proc msg done)))))

;; ── Buffer display ────────────────────────────────────────────────────────────
;; Route output back through comint-output-filter so comint keeps its
;; process-mark correct (required for history / input editing to work).

(defun beer-nrepl--display (proc msg done)
  "Insert MSG fields into the nREPL buffer via comint, adding prompt when DONE."
  (let ((parts '()))
    (when (plist-get msg :out)
      (push (plist-get msg :out) parts))
    (when (plist-get msg :err)
      (push (propertize (concat (plist-get msg :err) "\n") 'face 'error) parts))
    (when (plist-get msg :value)
      (push (concat (plist-get msg :value) "\n") parts))
    (when done
      (push beerlang-nrepl--prompt parts))
    (when parts
      (comint-output-filter proc (apply #'concat (nreverse parts))))))

;; ── Sending ops ──────────────────────────────────────────────────────────────

(defun beer-nrepl--send-op (op callback &rest kvs)
  "Send nREPL OP to the connected server with extra key-value pairs KVTS.
CALLBACK, if non-nil, is called with each response plist and removed on :done.
Nil CALLBACK means display responses in the buffer."
  (when (beerlang-nrepl--connected-p)
    (with-current-buffer beerlang-nrepl--buffer-name
      (let* ((id   (beer-nrepl--next-id))
             (wire (apply #'beer-nrepl--build-msg op id kvs)))
        (when callback
          (puthash id callback beer-nrepl--pending))
        (process-send-string (beerlang-nrepl--process) wire)))))

;; ── Interactive input sender ─────────────────────────────────────────────────
;; Installed as comint-input-sender so RET wraps input in an eval op.

(defun beer-nrepl--input-sender (proc string)
  "comint-input-sender: wrap STRING in an nREPL eval op and send to PROC."
  (let* ((id   (beer-nrepl--next-id))
         (wire (beer-nrepl--build-msg "eval" id ":code" string)))
    ;; No callback — filter displays result in buffer
    (process-send-string proc wire)))

;; ── Eldoc integration ────────────────────────────────────────────────────────
;; Async: send a :doc op, call eldoc's callback when the response arrives.
;; Works in both the nREPL buffer and .beer source buffers (when connected).

(defun beerlang-nrepl-eldoc (callback &rest _)
  "Eldoc backend: look up documentation for the symbol at point via nREPL.
CALLBACK is called with the docstring when the server responds.
Returns non-nil if a request was sent, nil if nREPL is not connected."
  (when (beerlang-nrepl--connected-p)
    (when-let ((sym (thing-at-point 'symbol t)))
      (beer-nrepl--send-op
       "doc"
       (lambda (msg)
         (when-let ((doc (plist-get msg :doc)))
           (funcall callback doc :thing sym :face 'font-lock-function-name-face)))
       ":sym" sym)
      ;; Return non-nil to tell eldoc we're handling it asynchronously
      t)))

;; ── nREPL buffer mode ─────────────────────────────────────────────────────────

(define-derived-mode beerlang-nrepl-mode comint-mode "Beer nREPL"
  "Major mode for the Beerlang network REPL buffer.
Input is wrapped in EDN eval ops; structured responses are parsed and
displayed.  Use `beer-nrepl--send-op' for tooling ops with callbacks."
  :syntax-table beerlang-mode-syntax-table

  ;; Buffer-local connection state
  (setq-local beer-nrepl--pending  (make-hash-table :test 'equal))
  (setq-local beer-nrepl--recv-buf "")

  ;; Tell comint to use our sender instead of raw process-send
  (setq-local comint-input-sender    #'beer-nrepl--input-sender)
  (setq-local comint-prompt-regexp   beerlang-nrepl--prompt-re)
  (setq-local comint-prompt-read-only t)
  (setq-local comint-use-prompt-regexp t)
  (setq-local comint-process-echoes  nil)

  (setq-local font-lock-defaults
              '(beerlang-font-lock-keywords nil nil nil nil))
  (font-lock-mode 1)
  (setq-local indent-line-function #'lisp-indent-line)
  (setq-local indent-tabs-mode     nil)
  (setq-local tab-width            2)
  (beerlang--configure-indentation)
  (setq-local paragraph-start beerlang-nrepl--prompt-re)

  ;; Eldoc: async doc lookup via :doc op
  (add-hook 'eldoc-documentation-functions #'beerlang-nrepl-eldoc nil t)
  (eldoc-mode 1))

;; ── Connect / disconnect ──────────────────────────────────────────────────────

(defun beerlang-connect (host port)
  "Connect to a beerlang nREPL server at HOST:PORT.
All eval commands route to the live process while connected.
Start the server inside beerlang with:
  (require 'beer.nrepl)
  (beer.nrepl/start! 7888)"
  (interactive
   (list (read-string (format "Host (default %s): " beerlang-nrepl-default-host)
                      nil nil beerlang-nrepl-default-host)
         (read-number "Port: " beerlang-nrepl-default-port)))
  (when (beerlang-nrepl--connected-p)
    (user-error "Already connected — disconnect first with M-x beerlang-disconnect"))
  (let* ((buf  (get-buffer-create beerlang-nrepl--buffer-name))
         (proc (open-network-stream "beerlang-nrepl" buf host port)))
    (with-current-buffer buf
      (beerlang-nrepl-mode)
      ;; Our filter replaces comint's default output filter
      (set-process-filter proc #'beer-nrepl--filter)
      ;; Emit the initial prompt through comint so process-mark is set
      (comint-output-filter proc beerlang-nrepl--prompt))
    (set-process-sentinel
     proc
     (lambda (p _)
       (unless (process-live-p p)
         (message "beerlang nREPL: disconnected from %s:%d" host port))))
    ;; Probe server capabilities (response displayed in buffer)
    (beer-nrepl--send-op "describe" nil)
    (pop-to-buffer buf)
    (message "beerlang nREPL: connected to %s:%d — ops: eval doc describe"
             host port)))

(defun beerlang-disconnect ()
  "Close the nREPL connection."
  (interactive)
  (let ((proc (beerlang-nrepl--process)))
    (if proc
        (progn (delete-process proc)
               (message "beerlang nREPL: disconnected"))
      (message "beerlang nREPL: not connected"))))

;; ================================================================
;; Unified send — prefers nREPL when connected
;; ================================================================

(defun beerlang-repl--ensure ()
  "Ensure the local REPL is running, starting it if necessary."
  (unless (beerlang-repl--running-p)
    (beerlang-run-repl))
  (beerlang-repl--process))

(defun beerlang-repl--active-buffer ()
  "Return the active REPL buffer name (nREPL if connected, else local)."
  (if (beerlang-nrepl--connected-p)
      beerlang-nrepl--buffer-name
    beerlang-repl--buffer-name))

(defun beerlang-repl--send-string (str)
  "Send STR for evaluation.  Routes to nREPL if connected, else local REPL."
  (let ((str (string-trim str)))
    (unless (string-empty-p str)
      (if (beerlang-nrepl--connected-p)
          (progn
            (beer-nrepl--send-op "eval" nil ":code" str)
            (when beerlang-repl-popup
              (display-buffer beerlang-nrepl--buffer-name)))
        (unless (string-suffix-p "\n" str)
          (setq str (concat str "\n")))
        (comint-send-string (beerlang-repl--ensure) str)
        (when beerlang-repl-popup
          (display-buffer beerlang-repl--buffer-name))))))

(defun beerlang-eval-region (start end)
  "Send the region between START and END to the Beerlang REPL."
  (interactive "r")
  (let ((code (buffer-substring-no-properties start end)))
    (beerlang-repl--send-string code)))

(defun beerlang-eval-last-sexp ()
  "Evaluate the sexp immediately before point."
  (interactive)
  (save-excursion
    (let ((end (point)))
      (backward-sexp)
      (beerlang-eval-region (point) end))))

(defun beerlang-eval-defun ()
  "Evaluate the top-level form (defun) containing or preceding point."
  (interactive)
  (save-excursion
    (end-of-defun)
    (let ((end (point)))
      (beginning-of-defun)
      (beerlang-eval-region (point) end))))

(defun beerlang-eval-buffer ()
  "Send the entire current buffer to the Beerlang REPL."
  (interactive)
  (beerlang-eval-region (point-min) (point-max)))

(defun beerlang-load-file (file)
  "Load FILE into the Beerlang REPL using the `load' function."
  (interactive (list (or (buffer-file-name)
                         (read-file-name "Load file: "))))
  (beerlang-repl--send-string
   (format "(load \"%s\")" (expand-file-name file))))

(defun beerlang-switch-to-repl ()
  "Switch to the active Beerlang REPL buffer.
Goes to the network REPL if connected, else starts/switches to the local REPL."
  (interactive)
  (if (beerlang-nrepl--connected-p)
      (pop-to-buffer beerlang-nrepl--buffer-name)
    (beerlang-repl--ensure)
    (pop-to-buffer beerlang-repl--buffer-name)))

(defun beerlang-set-ns ()
  "Set the REPL namespace to match the (ns ...) declaration in the buffer.
Falls back to `user' if no ns form is found."
  (interactive)
  (let ((ns (save-excursion
              (goto-char (point-min))
              (if (re-search-forward "^(ns[ \t\n]+\\(\\(?:\\sw\\|\\s_\\|\\.\\)+\\)" nil t)
                  (match-string-no-properties 1)
                "user"))))
    (beerlang-repl--send-string (format "(in-ns '%s)" ns))
    (message "Switched REPL to namespace: %s" ns)))

;; ================================================================
;; REPL major mode
;; ================================================================

(defvar beerlang-repl-mode-map
  (let ((m (make-sparse-keymap)))
    ;; Standard comint bindings remain; we add a few extras
    (define-key m (kbd "C-c C-l") #'beerlang-load-file)
    (define-key m (kbd "C-c C-n") #'beerlang-set-ns)
    m)
  "Keymap for `beerlang-repl-mode'.")

(define-derived-mode beerlang-repl-mode comint-mode "Beer REPL"
  "Major mode for the Beerlang interactive REPL buffer.

\\{beerlang-repl-mode-map}"
  :syntax-table beerlang-mode-syntax-table

  (setq-local comint-prompt-regexp         beerlang-repl--prompt-re)
  (setq-local comint-prompt-read-only       t)
  (setq-local comint-use-prompt-regexp      t)
  (setq-local comint-process-echoes         nil)

  ;; Re-use the source mode's font-lock rules
  (setq-local font-lock-defaults
              '(beerlang-font-lock-keywords nil nil nil nil))
  (font-lock-mode 1)

  ;; Indentation inside the REPL input area
  (setq-local indent-line-function #'lisp-indent-line)
  (setq-local indent-tabs-mode     nil)
  (setq-local tab-width            2)
  (beerlang--configure-indentation)

  ;; Paragraphs == top-level forms in the history
  (setq-local paragraph-start beerlang-repl--prompt-re))

;; ================================================================
;; Source buffer keymap additions (wired in beerlang.el)
;; ================================================================

(defun beerlang-repl--add-source-keys ()
  "Add REPL interaction keys to `beerlang-mode-map'."
  (define-key beerlang-mode-map (kbd "C-c C-z") #'beerlang-switch-to-repl)
  (define-key beerlang-mode-map (kbd "C-x C-e") #'beerlang-eval-last-sexp)
  (define-key beerlang-mode-map (kbd "C-c C-e") #'beerlang-eval-last-sexp)
  (define-key beerlang-mode-map (kbd "C-c C-c") #'beerlang-eval-defun)
  (define-key beerlang-mode-map (kbd "C-c C-r") #'beerlang-eval-region)
  (define-key beerlang-mode-map (kbd "C-c C-b") #'beerlang-eval-buffer)
  (define-key beerlang-mode-map (kbd "C-c C-l") #'beerlang-load-file)
  (define-key beerlang-mode-map (kbd "C-c C-n") #'beerlang-set-ns)
  (define-key beerlang-mode-map (kbd "C-c C-j") #'beerlang-connect)
  (define-key beerlang-mode-map (kbd "C-c C-q") #'beerlang-disconnect))

(defun beerlang-nrepl--setup-eldoc ()
  "Enable nREPL eldoc in the current source buffer when connected."
  (add-hook 'eldoc-documentation-functions #'beerlang-nrepl-eldoc nil t)
  (eldoc-mode 1))

;; Call this from beerlang-mode-hook once beerlang.el wires everything up.
;; beerlang-nrepl-eldoc is a no-op when not connected, so it's safe to
;; register unconditionally — it just returns nil until beerlang-connect runs.
(add-hook 'beerlang-mode-hook #'beerlang-nrepl--setup-eldoc)

(provide 'beerlang-repl)
;;; beerlang-repl.el ends here
