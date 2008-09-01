#!/bin/sh
awk '/malloc/ {print $3}' $1 | 
sed -e 's/malloc(\(.*\))/\1/g' |
sort -n
