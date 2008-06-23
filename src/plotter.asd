;; vim:filetype=lisp
(asdf:defsystem #:plotter
  :depends-on (#:sdl)
  :components ((:file "plotter")))

