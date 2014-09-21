#!/bin/bash

#pdflatex paper.tex

#for file in *.rst; do
#    rst2html $file > ${file}.html
#done

#options="--hyperlink-color=false --use-latex-abstract --use-latex-citations --template=default.tex"
options="--use-latex-abstract --use-latex-citations --template=default.tex"

for file in frontpage.aux main.aux paper.aux main.aux main.dvi main.latex main.log main.out main.rst.html main.toc introduction.rst.html jeff.rst.html main-tetex.tetex paper.aux paper.log paper.pdf paper.toc steve.rst.html; do
    rm -rf $file
done

rst2latex.py $options main.rst > main.latex

#latex main.latex 
#latex main.latex 
#dvips main.dvi

echo -n "Pass 1/3 "
pdflatex -interaction=nonstopmode main.latex 2> /tmp/errors.txt >/dev/null
if [[ "$?" == "0" ]]; then
    echo -ne "\rPass 2/3 "
    pdflatex -interaction=nonstopmode main.latex 2>&1 > /dev/null
    echo -ne "\rPass 3/3 "
    pdflatex -interaction=nonstopmode main.latex 2>&1 > /dev/null
    echo
    echo "------------------------------------------------------------"
    grep '\(<[A-Z]\+\|TODO\|XXX\)' *rst | grep -v '\(todo-checklist\|conclusion\)'
    echo "------------------------------------------------------------"
else
    echo
    echo "Erorrs in /tmp/errors.txt"
fi

#rst2xetex main.rst > main-xetex.xetex
#xetex main-xetex.xetex


