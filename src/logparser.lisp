(progn
  (require 'cl-ppcre)
  (require 'cl-fad)
  (use-package :cl-ppcre))

(defvar *memory-mappings* (make-hash-table :test 'equal))

(with-open-file (s #P"aftonbladet/start-to-aftonbladet.log")
  (do ((line (read-line s) (read-line s nil 'eof)))
      ((eq line 'eof) nil)
      (multiple-value-bind (match regs)
            (scan-to-strings "MEM: (0x\\w+) malloc\\((\\d+)\\) => (0x\\w+)" line)
        (when regs
          (setf (gethash (aref regs 2) *memory-mappings*)
                (aref regs 1))))))

(gethash "0x0903aebb" *memory-mappings*)
