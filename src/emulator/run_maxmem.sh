#!/bin/bash

#set -x

if [[ "$1" == "" || ! -f "$1" ]]; then
echo "opsfile req'd"
	exit
fi

if [[ "$ALLOCATOR" == "" || ! -f "$ALLOCATOR" ]]; then
    echo "ALLOCATOR not set or does not exist. Set it to the path to test program, e.g. ALLOCATOR=./plot_dlmalloc"
    exit
fi


if [[ "$CORES" == "" ]]; then
    CORES=$(grep -c ^processor /proc/cpuinfo)
    let CORES=2*$CORES
fi
export DATAPOINTS=1500 # requested -- will be adjusted down if neccessary.

export opsfile=$1
export RESULTFILE=$(basename $opsfile)-$(basename $ALLOCATOR)-allocstats

echo -n "Calculating theoretical peak mem used by the allocator ($ALLOCATOR --peakmem $opsfile)... "
# XXX: re-enable this
export peakmem=$($ALLOCATOR --peakmem $opsfile 2> /dev/null)
export theory_peakmem=$peakmem

#peakmem=$(echo "$peakmem*1.05/1" | bc)
#echo "$theory_peakmem bytes. Increased by 5% => $peakmem bytes"
echo "($theory_peakmem bytes)"

# alright, try to figure out how small we can make peakmem while still not getting OOMs.
# this could take some time...

done=0
fullcount=$(wc -l $opsfile | awk '{print $1}')

echo "fullcount = $fullcount"

export OPS_COUNT=$(grep '\(N\|F\)' $opsfile | wc -l | awk '{print $1}')

echo "ops_count (N/F ops) = $OPS_COUNT"

while [[ "$done" != "1" ]]; do
    echo -n "* Calculating maxmem for peakmem $peakmem bytes..."

    #echo "($ALLOCATOR)--maxmem ($opsfile) ($RESULTFILE) ($fullcount) ($peakmem) ($theory_peakmem)"
    #echo $ALLOCATOR --maxmem $opsfile $RESULTFILE $fullcount $peakmem $theory_peakmem
    $ALLOCATOR --maxmem $opsfile $RESULTFILE $fullcount $peakmem $theory_peakmem > /dev/null 2>&1
    status=$?
    if [[ "$status" != "0" ]]; then
        # oom, bump by 5% and retry.
        peakmem=$(echo "$peakmem*1.05/1" | bc)
        echo -e "OOM! Bump by 5% up to $peakmem bytes"
    else
        echo -e "OK!\n"
        break
    fi
done

echo > $RESULTFILE

# each run "discards" the previous ones, i.e. doesn't try to do max-size allocs for any but the last

if [[ "$DATAPOINTS" -gt "$OPS_COUNT" ]]; then
    DATAPOINTS=$OPS_COUNT
fi
ENDPOINTS=$DATAPOINTS

declare -a corejobs

i=0
for start in $(seq 0 10 $ENDPOINTS); do
    s="${corejobs[$i]} $start"
    corejobs[$i]=$s
    let i=i+1
    if [[ "$i" == "$CORES" ]]; then
        i=0
    fi
done

echo "Starting $CORES jobs."

rm -rf donefile.*

let jobs=$CORES-1

for i in $(seq 0 $jobs); do
    ./run_maxmem_payload.sh donefile.$i ${corejobs[$i]} &
done

echo "Waiting for run to finish."

continue=1
while [[ "$continue" == "1" ]]; do
    sleep 1s
    continue=0
    for i in $(seq 0 $jobs); do
        if [[ ! -f "donefile.$i" ]]; then
            #echo "* $i not yet done."
            continue=1
        else
            echo "* $i done"
        fi
    done
done

rm -rf donefile.*

#ls -1 ${RESULTFILE}.part* | sort -n | xargs ls # cat >> $RESULTFILE

cat ${RESULTFILE}.part* >> $RESULTFILE
echo ']' >> $RESULTFILE

rm -rf ${RESULTFILE}.part*


# 
# # number of operations to perform..
# # each run "discards" the previous ones, i.e. doesn't try to do max-size allocs for any but the last
# echo "$ALLOCATOR --maxmem $opsfile $i $peakmem $theory_peakmem (of $count)"
# i=0
# while [[ "$i" != "$count" ]]; do
# #for i in $(seq 0 $count); do
#     echo -ne "\r                               \r$i / $count "
#     $ALLOCATOR --maxmem $opsfile $i $peakmem $theory_peakmem > /dev/null 2>&1
# 
#     let i=$i+1
# done
# echo ']' >> dlmalloc.alloc-stats

#python grapher.py dlmalloc.maxmem-stats
echo "Generating graph from $RESULTFILE"
python grapher.py $RESULTFILE

