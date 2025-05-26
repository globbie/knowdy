;;; gsl_mode.el --- GSL major mode

 ;; This file is free software; you can redistribute it and/or modify
 ;; it under the terms of the GNU General Public License as published by
 ;; the Free Software Foundation; either version 2, or (at your option)
 ;; any later version.

 ;; This file is distributed in the hope that it will be useful,
 ;; but WITHOUT ANY WARRANTY; without even the implied warranty of
 ;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ;; GNU General Public License for more details.

 ;; You should have received a copy of the GNU General Public License
 ;; along with GNU Emacs; see the file COPYING.  If not, write to
 ;; the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 ;; Boston, MA 02111-1307, USA.

 ;;; Commentary:

 ;; 

 ;;; Code:

 (defvar gsl-mode-hook nil)

 (defvar gsl-mode-map
   (let ((map (make-sparse-keymap)))
     (define-key map [foo] 'gsl-do-foo)
     map)
   "Keymap for `gsl-mode'.")

 (defvar gsl-mode-syntax-table
   (let ((st (make-syntax-table)))
     (modify-syntax-entry ?- "w" st)
     (modify-syntax-entry ?\( ". 1" st)
     (modify-syntax-entry ?\) ". 4" st)
     (modify-syntax-entry ?* ". 23" st)
     st)
   "Syntax table for `gsl-mode'.")

(defconst gsl-font-lock
  (list
   '("\\[\\(gloss\\)" . (1 font-lock-string-face))
   '("{[!]?\\(proc\\)" . (1 font-lock-keyword-face))
   '("{[!]?\\(cls\\)" . (1 font-lock-keyword-face))
   '("{\\(is\\)[\s]+" . (1 font-lock-keyword-face))
   '("{\\(cls\\)[\s]+" . (1 font-lock-keyword-face))
   '("{\\(do\\)[\s]+" . (1 font-lock-keyword-face))
   '("{\\(t\\)[\s]+" . (1 font-lock-keyword-face))
   '("{\\(inner\\)[\s]+" . (1 font-lock-keyword-face))
   '("{inner[\s]+\\([^{}]+\\)" . (1 font-lock-variable-name-face))
   '("{\\(ref\\)[\s]+" . (1 font-lock-keyword-face))
   '("{ref[\s]+\\([^{}]+\\)" . (1 font-lock-variable-name-face))
   '("{\\(str\\)[\s]+" . (1 font-lock-keyword-face))
   '("{str[\s]+\\([^{}]+\\)" . (1 font-lock-variable-name-face))
   '("{\\(num\\)[\s]+" . (1 font-lock-keyword-face))
   '("{uint[\s]+\\([^{}]+\\)" . (1 font-lock-variable-name-face))
   '("{\\(bool\\)[\s]+" . (1 font-lock-keyword-face))
   '("{bool[\s]+\\([^{}]+\\)" . (1 font-lock-variable-name-face))
   '("{\\(date\\)[\s]+" . (1 font-lock-keyword-face))
   '("{date[\s]+\\([^{}]+\\)" . (1 font-lock-variable-name-face))
   '("{[!]?proc \\([^][{}]+\\)" . (1 font-lock-function-name-face))
   '("{is \\([^][{}]+\\)" . (1 font-lock-constant-face))
   '("{[!]?class \\([^][{}]+\\)" . (1 font-lock-type-face))
   '("{[!]?cls \\([^{}]+\\)}" . (1 font-lock-constant-face))
   '("{do[\s]+\\([^{}]+\\)" . (1 font-lock-constant-face))
   '("{\\([^][\s{}]+\\)" . (1 font-lock-variable-name-face)))
   "Minimal highlighting expressions for `gsl-mode'.")

 ;;;###autoload
 (define-derived-mode gsl-mode fundamental-mode "GSL"
   "A major mode for editing GSL files."
   (setq font-lock-defaults '(gsl-font-lock))
   (set-syntax-table gsl-mode-syntax-table)

)


 (provide 'gsl)
 ;;; gsl_mode.el ends here
