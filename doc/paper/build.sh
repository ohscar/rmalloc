#!/bin/bash

#pdflatex paper.tex

#for file in *.rst; do
#    rst2html $file > ${file}.html
#done

rst2latex main.rst > main.latex
pdflatex main.latex
pdflatex main.latex

#rst2xetex main.rst > main-xetex.xetex
#xetex main-xetex.xetex

