#!/bin/bash

#
# Run split-up work. Many instances of this runs.
#
#
# Arguments:
#
#   ./run_maxmem_part.sh N1 N2 N3 N4 N5
#
# Runs  plot_dlmaloc for N1..N1+9, N2..N2+9, and so on.
#
donefile=$1
shift
echo "$donefile running on $*"
while [[ "$1" != "" ]]; do
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
        #$ALLOCATOR --maxmem $opsfile $RESULTFILE $trueindex $peakmem $theory_peakmem
        $ALLOCATOR --maxmem $opsfile $RESULTFILE $trueindex $peakmem $theory_peakmem > /dev/null 2>&1
        #echo "$donefile $ALLOCATOR --maxmem $opsfile $RESULTFILE $trueindex $peakmem $theory_peakmem ($i $SKIPDATA $OPS_COUNT $DATAPOINTS)"
    done
done

echo > $donefile

