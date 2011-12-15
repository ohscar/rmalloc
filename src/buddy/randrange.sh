#!/bin/bash

if [[ "$3" == "" ]]; then
    echo "$0 min max count"
    exit
fi

for i in $(seq $3); do
    let a="$1 + ($RANDOM % $2)"
    echo -n "$a "
done
echo


