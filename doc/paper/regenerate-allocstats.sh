#!/bin/bash

python ../../src/steve/run_graphs_from_allocstats.py allocstats/result-opera-google allocstats/result.opera.google*allocstats > /tmp/a1
python ../../src/steve/run_graphs_from_allocstats.py allocstats/result-soffice allocstats/result.soffice*allocstats > /tmp/a2

