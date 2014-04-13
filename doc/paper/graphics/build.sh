#!/bin/bash

for file in *.dot; do
    base=${file%.dot}
    ./render.sh ${base}
done

