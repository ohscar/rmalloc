#!/bin/bash

#pdflatex paper.tex

#for file in *.rst; do
#    rst2html $file > ${file}.html
#done

#options="--hyperlink-color=false --use-latex-abstract --use-latex-citations --template=default.tex"
options="--use-latex-abstract --use-latex-citations --template=default.tex"

rst2latex $options main.rst > main.latex

#latex main.latex 
#latex main.latex 
#dvips main.dvi

pdflatex main.latex
pdflatex main.latex

#rst2xetex main.rst > main-xetex.xetex
#xetex main-xetex.xetex

