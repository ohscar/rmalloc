#!/bin/bash

#
# Run split-up work. Many instances of this runs.
#
# Input:
# ALLOCATOR: path to file
# RESULTFILE: where to put the data
# OPS_COUNT
# DATAPOINTS
#
# Arguments:
#
#   ./run_maxmem_part.sh N1 N2 N3 N4 N5
#
# Runs  plot_dlmaloc for N1..N1+9, N2..N2+9, and so on.
#

donefile=$1
shift
echo "- $donefile running on $*"
finished="0"
while [[ "$1" != "" && "$finished" == "0" ]]; do
    start=$1
    let end=$start+9

    shift

    # number of points where we measure max alloc'able amount. should be approx 1000 or so.
    let SKIPDATA=$OPS_COUNT/$DATAPOINTS
    #echo "$donefile: running from $start to $end: $ALLOCATOR --maxmem $opsfile [i] $peakmem $theory_peakmem (of $OPS_COUNT)"
    i=0
    for i in $(seq $start $end); do
        let trueindex=$i*$SKIPDATA
        #echo -ne "\r                               \r$trueindex / $count ($i of $DATAPOINTS)"
        $ALLOCATOR --allocstats $opsfile $RESULTFILE $KILLPERCENT $trueindex $peakmem $theory_peakmem >> /tmp/log.txt 2>&1
        status=$?
        if [[ "$status" != "0" ]]; then
            finished="1"
            break
        fi
        #$ALLOCATOR --allocstats $opsfile $RESULTFILE $trueindex $peakmem $theory_peakmem > /dev/null 2>&1
        echo "$donefile $ALLOCATOR --allocstats $opsfile $RESULTFILE $KILLPERCENT $trueindex $peakmem $theory_peakmem ($i $SKIPDATA $OPS_COUNT $DATAPOINTS)" >> /tmp/log.txt 2>&1
    done
done

echo > $donefile

