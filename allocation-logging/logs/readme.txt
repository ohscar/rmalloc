======================================
 Allocation Data
======================================
Logging was done on a Core-2.1 LinGogi build (Desktop profile) w/ LOG_ALLOCATIONS enabled.

Each run is logged by appending all allocation info to a log, and then after
waiting a few minutes for the page to stabilize, the number of lines in the
logfile is noted, and the lines between the current starting point and the
last line is saved to a file.  This is repeated at each step, in order to get
the data in isolation.  The full flow can be retrieved by concatenating all
files in a directory in the correct order.

The number in parens is the size of the unpacked log directory.

GP (20 Mb)
===========
1. Start to GP (stabilize one minute) to exit

Blank (12 Mb)
==============
1. start-to-blank.log
2. blank-to-exit.log

Aftonbladet (35 Mb)
=====================
1. start-to-aftonbladet.log
2. aftonbladet-to-blank.log
3. blank-to-exit.log

Aftonbladet - GP (39 Mb)
=============================
1. start-to-aftonbladet.log
2. aftonbladet-to-gp.log
3. gp-to-blank.log
4. blank-to-exit.log

