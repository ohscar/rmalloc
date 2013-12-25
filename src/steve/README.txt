=========================================
 Steve
=========================================

Generate ops
============
Run::

   memtrace-to-ops/translate-memtrace-to-ops.py path/to/memtrace/app

Generates::

    app.ops


Generate animation
==================
Run::

    ALLOCATOR=./drivers/plot_dlmalloc ./run_memory_frag_animation.sh result.soffice-ops

Generates::

    result.soffice-ops-animation.avi

Generate allocstats
===================
Run::

    ALLOCATOR=./drivers/plot_dlmalloc ./run_allocator_stats.sh result.soffice-ops

Generates::

    result.soffice-ops.allocstats

Generate single-allocator graph from allocstats
================================================
Run::

    python run_graphs_from_allocstats.py result.soffice-ops

Generates::

    plot-<driver>-<opsfile>.png

Generate comparison graph from many drivers' allocstats
=========================================================
Run::

    python run_graphs_from_allocstats.py soffice result.soffice-ops-dlmalloc result.soffice-ops-rmmalloc [...]

Generates:

    soffice.png


