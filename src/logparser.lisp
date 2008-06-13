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
(defvar *memory-mappings-counter* 0)
(defvar *alloc-sequence* nil)

(progn
  (clrhash *memory-mappings*)
  (setf *memory-mappings-counter* 0)
  (setf *alloc-sequence* nil))

(defun next-memory-mapping-counter ()
  (incf *memory-mappings-counter*))

(defun pointer->id (pointer)
  "Return the counter corresponding to the pointer. Adds the pointer to the mappings table and assigns a counter if it doesn't already exist."
  (multiple-value-bind (id presentp) (gethash pointer *memory-mappings*)
   (if presentp
       id
       (setf (gethash pointer *memory-mappings*) (next-memory-mapping-counter)))))

(defun replace-pointer (old new)
  "Replace pointer old with new in the mapping, but keep the counter."
  (multiple-value-bind (counter presentp) (gethash old *memory-mappings*)
    (when presentp
      (remhash old *memory-mappings*))
    (setf (gethash new *memory-mappings*) counter)))

;;; output a list of allocations, where the pointers have been remapped to 
;;; a monotonically increasing integer (used to name variables, or possibly index into a list of pointers)
;;; realloc() is treated like a free() followed directly by an alloc()

(with-open-file (s #P"aftonbladet/start-to-aftonbladet.log")
  (do ((line (read-line s) (read-line s nil 'eof)))
      ((eq line 'eof) nil)
      (multiple-value-bind (match regs) (scan-to-strings "MEM: 0x\\w+ malloc\\((\\d+)\\) => (0x\\w+)" line)
                           (declare (ignore match))
                           (when regs
                                 ;; add to known pointers list
                                 (let ((id (pointer->id (aref regs 1))))
                                      ;; output a malloc request
                                      (push `(malloc :size ,(read-from-string (aref regs 0)) :id ,id)
                                            *alloc-sequence*))))
      (multiple-value-bind (match regs) (scan-to-strings "MEM: 0x\\w+ realloc\\((\\w+),(\\d+)\\) => (0x\\w+)" line)
                           (declare (ignore match))
                           (when regs
                                 (push `(free :id ,(pointer->id (aref regs 0)))
                                       *alloc-sequence*)
                                 (replace-pointer (aref regs 0) (aref regs 2))
                                 (let ((id (pointer->id (aref regs 2))))
                                      (push `(malloc :size ,(read-from-string (aref regs 0)) :id ,id)
                                            *alloc-sequence*))))
      (multiple-value-bind (match regs) (scan-to-strings "MEM: 0x\\w+ free\\((\\w+)\\)" line)
                           (declare (ignore match))
                           (when regs
                                 (push `(free :id ,(pointer->id (aref regs 0)))
                                       *alloc-sequence*))))
  (setf *alloc-sequence* (nreverse *alloc-sequence*))
  nil)




