#!/bin/bash

if [[ ! -f "$1.dot" ]]; then
    echo "$0: filename (without .dot)"
    exit
fi


dot -Tpng $1.dot -o $1.png

