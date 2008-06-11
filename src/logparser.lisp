#| Installation
(progn
  (require 'asdf)
  (require 'asdf-install)
  (asdf-install:install :cl-fad)
  (asdf-install:install :cl-ppcre))
|#

(progn
  (require 'cl-ppcre)
  (require 'cl-fad)
  (use-package :cl-ppcre))

(defvar *memory-mappings* (make-hash-table :test 'equal))

(with-open-file (s #P"aftonbladet/start-to-aftonbladet.log")
  (do ((line (read-line s) (read-line s nil 'eof)))
      ((eq line 'eof) nil)
      (multiple-value-bind (match regs) (scan-to-strings "MEM: 0x\\w+ malloc\\((\\d+)\\) => (0x\\w+)" line)
        (declare (ignore match))
        (when regs
          (setf (gethash (aref regs 1) *memory-mappings*)
              (aref regs 0))))
      (multiple-value-bind (match regs) (scan-to-strings "MEM: 0x\\w+ realloc\\((\\w+),(\\d+)\\) => (0x\\w+)" line)
        (declare (ignore match))
        (when regs
               #|(format t "relloc(~A) ~A => ~A~%"
                       (aref regs 1) (aref regs 0) (aref regs 2))|#))
      (multiple-value-bind (match regs) (scan-to-strings "MEM: 0x\\w+ free\\((\\w+)\\)" line)
        (declare (ignore match))
        (when regs
              #| (format t "free(~A)~%" (aref regs 0))|# ))))

(let ((bytes (read-from-string (gethash "0x095d2670" *memory-mappings*))))
  (format t "bytes read: ~A~%" bytes))
