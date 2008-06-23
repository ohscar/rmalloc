#| plotter.lisp

Plots allocation data as read by stdin, one s-exp per line.
First, total amount of memory is read, a window is displayed with UNUSED-color,
and then gradually replaced by ALLOC/FREE-colors.)

Input (N = size in bytes)
(memory-setup N)
(alloc :start N :size N)
(free :start N :size N)

Idea::

  $ mkfifo logdata
  $ ./main 2>/dev/null > logdata &
  $ sbcl --load plotter.lisp --eval '(plotter #P"logdata")'
  
(then, start recordmydesktop for great justice)
|#
(defpackage #:plotter
  (:use #:common-lisp)
  (:export #:main))

(in-package #:plotter)

(defvar *width* 1200)
(defvar *height* 800)
(defvar *x*)
(defvar *y*)
(defvar *drawingp* nil)
(defconstant +memory-size+ (* 8 1024 1024 ))
(defconstant +input-file+ #P"logdata.pipe")
(defvar *frame-counter* 0)

(defun init-sdl ()
  (sdl:init (logior sdl:+init-video+))
  (let ((surface (sdl:set-video-mode *width* *height* 16
                                     (logior sdl:+swsurface+))))
     (when (sgum:null-pointer-p surface)
           (error "Unable to set video mode"))
     (sdl:wm-set-caption "Plotter" nil)
     surface))

(defun sdl-event-loop (surface update-fn)
  (sdl:event-loop
   (:mouse-motion (x y xrel yrel state)
                  (when *drawingp*
                        (cl-sdl:draw-pixel surface x y 128 128 128)))
   (:mouse-button-down (x y button)
                       (setf *x* x
                             *y* y)
                       (case button
                             (:right (return))
                             (:left (setf *drawingp* t))))
   (:mouse-button-up (x y button)
                     (setf *drawingp* nil))
   (:quit ()
          (return))
   (:idle ()
          (funcall update-fn))))

(defun alloc->lines (start size)
  "Calculate a list of lines based on the start and size of a memory allocation,
scaled to the dimensions of the window and the total amount of memory.
FIXME: Make sure to calculate all lines between start-y and end-y.  Better yet, use rectangle?"
  (let* ((scaled-start (floor (* *width* *height* (/ start +memory-size+))))
         (start-x (mod scaled-start *width*))
         (start-y (floor (/ scaled-start *width*)))
         (scaled-size (floor (* *width* *height* (/ size +memory-size+))))
         (end-x (+ start-x (mod scaled-size *width*)))
         (end-y (+ start-y (floor (/ scaled-size *width*))))
         (lines ()))
      ;; spanning multiple lines
      (cond ((not (= start-y end-y))
             ;; the first line will always end at the end
             (push (list start-x start-y (1- *width*) start-y) lines)
             ;; the rest, except the last, will be full lines
             (loop for y from (1+ start-y) to (1+ end-y)
                   do (push (list 0 y (1- *width*) y) lines))
             ;; the final line will always start at zero
             (push (list 0 end-y end-x end-y) lines))
            (t
             (push (list start-x start-y end-x end-y) lines)))
      (nreverse lines)))

(defun read-and-draw-alloc-data (surface s)
  "Grab and READ next line of allocation data from stream S, then draw on screen."
  (let ((line (read-line s nil 'eof)))
     (unless (eql line 'eof)
         (let* ((alloc-data (read-from-string line))
                (start (getf (rest alloc-data) :start))
                (size (getf (rest alloc-data) :size)))

          (case (first alloc-data)
                (alloc (dolist (line (alloc->lines start size))
                        (cl-sdl:draw-line surface (first line) (second line)
                                                  (third line) (fourth line)
                                                  128 0 0
                                                  :update-p (zerop (mod (incf *frame-counter*) 1000)))
                        ;;(format t "ALLOC ~s (~A ~A)~%" line start size)
                        ))
                (free (dolist (line (alloc->lines start size))
                        (cl-sdl:draw-line surface (first line) (second line)
                                                  (third line) (fourth line)
                                                  0 128 0)
                        ;;(format t "FREE ~s (~A ~A)~%" line start size)
                        ))
                        
             )))))

(defun main ()
  (unwind-protect
   (progn
    (format t "Waiting for input data...~%")
    (with-open-file (s +input-file+)
                    (let* ((surface (init-sdl)))
                          (sdl-event-loop surface #'(lambda () (funcall #'read-and-draw-alloc-data surface s))))))
   (sdl:quit)))

