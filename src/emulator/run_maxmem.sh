#!/bin/bash

opsfile=result.soffice-ops
maxmemfile=dlmalloc.maxmem-stats

echo > $maxmemfile

echo -n "Calculating peak mem... "
peakmem=$(./plot_dlmalloc --peakmem $opsfile 2> /dev/null)
peakmem=$(echo "$peakmem*1.5/1" | bc)
echo "$peakmem (scaled by 1.5) bytes"
count=$(wc -l $opsfile | awk '{print $1}')
for i in $(seq 0 $count); do
    echo "./plot_dlmalloc --maxmem $opsfile $i $peakmem (of $count)"
    ./plot_dlmalloc --maxmem $opsfile $i $peakmem
done
echo ']' >> dlmalloc.alloc-stats

#python grapher.py dlmalloc.maxmem-stats
python grapher.py dlmalloc.alloc-stats

