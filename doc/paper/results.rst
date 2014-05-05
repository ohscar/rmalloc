Limitations
=======================================================
Both tcmalloc and jemalloc perform poorly without mmap. tcmalloc did not manage to finish for Opera with
TCMALLOC_SKIP_MMAP=1.

Input data
=============
Measuring an allocator must be done in conjunction with input data. These are the applications I've used.

* Opera v12.0 loading http://www.google.com
* LibreOffice 4.0.2.2 (soffice) and exiting
* sqlite 2.8.17 - ubuntu 13.04 - loading an SQL file of TODO megabytes
* zip 3.0 - ubuntu 13.04 - loading a set of files TODO # items and MB size
* ls 8.20 - ubuntu 13.04 - displaying a directory
* cfrac 3.5.1 (3.51?) - just running it

opera
=========
.. figure:: allocstats/result-opera-google.png
   :scale: 30%

   :label:`resultsopera` Opera results. tcmalloc DNF.

This yielded an 8.4 GB large opsfile. See Figure :ref:`resultsopera`.

libreoffice
=============
.. figure:: allocstats/result-soffice.png
   :scale: 30%
   
   :label:`resultslibreoffice` LibreOffice results. Poor performance of jemalloc.

Foo bralfar target see Figure :ref:`resultslibreoffice`.


sqlite
=============

zip
============

ls
===============

cfrac
===============

