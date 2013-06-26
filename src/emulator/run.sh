#!/bin/bash

GDB="gdb --args" 
#GDB=

$GDB ./plot_dlmalloc result.soffice-ops

# ./plot_dlmalloc result.soffice-ops
#ffmpeg -f image2 -r 20 -i result.soffice-ops-plot-%6d.png -r 30  -vcodec mjpeg -sameq out.avi

python grapher.py dlmalloc.alloc-stats

