#!/bin/bash

#python ../../src/steve/run_graphs_from_allocstats.py allocstats/result-opera-google allocstats/result.opera.google*allocstats > /tmp/a1
#python ../../src/steve/run_graphs_from_allocstats.py allocstats/result-soffice allocstats/result.soffice*allocstats > /tmp/a2

GR=../../src/steve/run_graphs_from_allocstats.py 

i=1
for app in ls latex soffice sqlite tar opera-blank2 opera-google; do
    thedir=
    testdir=allocstats/${app}-allocstats-huvudfoting
    [[ -d $testdir ]] && thedir=$testdir 
    testdir=allocstats/${app}-allocstats-haddock
    [[ -d $testdir ]] && thedir=$testdir 

    if [[ -d $thedir ]]; then
        echo "Processing $app in $thedir"
        python $GR allocstats/result-${app} ${thedir}/*allocstats > /tmp/allocstat-${app}
    else
        echo "Skipping $testdir"
    fi
    let i=i+1
done

