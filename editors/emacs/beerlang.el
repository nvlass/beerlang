;;; beerlang.el --- Beerlang language support for Emacs -*- lexical-binding: t; -*-

;; Copyright (C) 2025 Beerlang Contributors
;; Author: Beerlang Contributors
;; URL: https://github.com/nvlass/beerlang
;; Keywords: languages lisp
;; Version: 0.1.0
;; Package-Requires: ((emacs "27.1"))

;;; Commentary:
;; Complete Emacs support package for the Beerlang programming language.
;;
;; Quick start — add to your init.el:
;;
;;   (add-to-list 'load-path "/path/to/beerlang/editors/emacs")
;;   (require 'beerlang)
;;
;;   ;; Optional: point at your beerlang binary and library path
;;   (setq beerlang-repl-program "/path/to/bin/beerlang"
;;         beerlang-repl-beerpath "/path/to/beerlang/lib")
;;
;; Then open any .beer file.  Use M-x beerlang-run-repl to start the REPL.
;;
;; Layer overview:
;;
;;   beerlang-mode.el   — syntax highlighting, indentation, sexp nav
;;   beerlang-repl.el   — comint REPL (local process)
;;   beerlang-nrepl.el  — network REPL client (TCP, coming soon)
;;
;; Planned:
;;   beerlang-nrepl.el  — structured eval/doc/complete over TCP
;;   eldoc integration  — arity / docstring in echo area
;;   flycheck checker   — linter errors as inline annotations

;;; Code:

(eval-when-compile
  (require 'beerlang-mode)
  (require 'beerlang-repl))
(require 'beerlang-mode)
(require 'beerlang-repl)

;; Wire REPL interaction keys into the source mode keymap.
;; This is done here (not in beerlang-repl.el) so that loading only
;; beerlang-mode.el without the REPL layer stays clean.
(beerlang-repl--add-source-keys)

;; ================================================================
;; Project integration
;; ================================================================

(defcustom beerlang-project-file "beer.edn"
  "Name of the Beerlang project descriptor file."
  :type 'string
  :group 'beerlang)

(defun beerlang-project-root ()
  "Return the root of the current Beerlang project, or nil.
Walks upward from `default-directory' looking for `beerlang-project-file'."
  (locate-dominating-file default-directory beerlang-project-file))

(defun beerlang-run-project ()
  "Run the current Beerlang project via `beer run' in a compilation buffer."
  (interactive)
  (let ((root (or (beerlang-project-root)
                  (error "No %s found in parent directories" beerlang-project-file))))
    (let ((default-directory root))
      (compile "beer run"))))

(defun beerlang-run-tests ()
  "Run the project's test suite via `beer test'."
  (interactive)
  (let ((root (or (beerlang-project-root)
                  (error "No %s found in parent directories" beerlang-project-file))))
    (let ((default-directory root))
      (compile "beer test"))))

;; ================================================================
;; Convenience: start REPL rooted at the project
;; ================================================================

(defun beerlang-run-project-repl ()
  "Start the Beerlang REPL with BEERPATH set to the project's lib directory."
  (interactive)
  (let* ((root (beerlang-project-root))
         (beerlang-repl-beerpath
          (if root
              (expand-file-name "lib" root)
            beerlang-repl-beerpath)))
    (beerlang-run-repl)))

;; ================================================================
;; Menubar entry (when menu-bar-mode is active)
;; ================================================================

(easy-menu-define beerlang-menu beerlang-mode-map
  "Menu for Beerlang mode."
  '("Beerlang"
    ["Start / Switch to REPL"  beerlang-switch-to-repl  t]
    ["Run Project REPL"        beerlang-run-project-repl t]
    "---"
    ["Eval Last Sexp"          beerlang-eval-last-sexp   t]
    ["Eval Top-Level Form"     beerlang-eval-defun       t]
    ["Eval Region"             beerlang-eval-region      :active mark-active]
    ["Eval Buffer"             beerlang-eval-buffer      t]
    ["Load File…"              beerlang-load-file        t]
    ["Set REPL Namespace"      beerlang-set-ns           t]
    "---"
    ["Run Project"             beerlang-run-project      t]
    ["Run Tests"               beerlang-run-tests        t]))

(provide 'beerlang)
;;; beerlang.el ends here
