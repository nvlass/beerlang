;;; beerlang-repl.el --- Beerlang REPL (local process) -*- lexical-binding: t; -*-

;; Copyright (C) 2025 Beerlang Contributors
;; Keywords: languages lisp
;; Package-Requires: ((emacs "27.1") (beerlang-mode "0.1.0"))

;;; Commentary:
;; Comint-based REPL integration for Beerlang.
;;
;; Usage:
;;   M-x beerlang-run-repl        — start or switch to the REPL buffer
;;
;; From a .beer source buffer (after (require 'beerlang)):
;;   C-c C-z  — switch to REPL (starting it if needed)
;;   C-x C-e  — eval sexp before point
;;   C-c C-c  — eval top-level form (defun) around point
;;   C-c C-r  — eval active region
;;   C-c C-b  — eval entire buffer
;;   C-c C-l  — load file into REPL
;;   C-c C-n  — set REPL namespace to match current file's (ns ...)

;;; Code:

(require 'comint)
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
  "Name of the Beerlang REPL buffer.")

(defconst beerlang-repl--prompt-re "^[a-zA-Z._-]*:[0-9]+>\\s-?"
  "Regular expression matching a Beerlang REPL prompt.")

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
;; Sending code to the REPL
;; ================================================================

(defun beerlang-repl--ensure ()
  "Ensure the REPL is running, starting it if necessary.
Returns the REPL process."
  (unless (beerlang-repl--running-p)
    (beerlang-run-repl))
  (beerlang-repl--process))

(defun beerlang-repl--send-string (str)
  "Send STR to the running Beerlang REPL."
  (let ((proc (beerlang-repl--ensure))
        (str  (string-trim str)))
    (when (and proc (not (string-empty-p str)))
      ;; Make sure each send ends with a newline so the REPL processes it
      (unless (string-suffix-p "\n" str)
        (setq str (concat str "\n")))
      (comint-send-string proc str)
      (when beerlang-repl-popup
        (display-buffer beerlang-repl--buffer-name)))))

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
  "Switch to the Beerlang REPL buffer, starting it if needed."
  (interactive)
  (beerlang-repl--ensure)
  (pop-to-buffer beerlang-repl--buffer-name))

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
  (define-key beerlang-mode-map (kbd "C-c C-n") #'beerlang-set-ns))

(provide 'beerlang-repl)
;;; beerlang-repl.el ends here
