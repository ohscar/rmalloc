.. DOC: nifty table layout: http://tex.stackexchange.com/questions/102512/remove-vertical-line-in-tabular-head

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
This is the largest input application of the examples used. (The opsfile is 8,4 Gb). As mentioned before (TODO: where?),
the full opsfile is only useful for lifetime calculations. An opsfile with complete locking/unlocking and malloc/free
information is significantly smaller.

See Figure :ref:`resultsopera`.

.. figure:: allocstats/result-opera-google.png
   :scale: 50%

   :label:`resultsopera` Opera results. tcmalloc DNF.

.. raw:: latex

   \begin{table}
   \begin{tabular}{r | l c c c}
   \hline
   \multicolumn{5}{c}{\bf Speed} \\
   \hline
   {\bf Driver} & {\bf Penalty (count)} & {\bf Penalty (weighted)} & {\bf Best} & {\bf Worst} \\
   \hline
   rmmalloc & 16\% & 0\% & 0\% & 9\% \\
   dlmalloc & 29\% & 1\% & 100\% & 22\% \\
   jemalloc & 54\% & 5\% & 0\% & 68\% \\
   \hline
   \end{tabular}
   \caption{result-opera-google-speed}
   \label{table:result-opera-google-speed}
   \end{table}


.. raw:: latex

   \begin{table}
   \begin{tabular}{r | l c c c}
   \hline
   \multicolumn{5}{c}{\bf Space} \\
   \hline
   {\bf Driver} & {\bf Penalty (count)} & {\bf Penalty (weighted)} & {\bf Best} & {\bf Worst} \\
   \hline
   dlmalloc & 0\% & 0\% & 99\% & 100\% \\
   rmmalloc & 33\% & 11\% & 0\% & 0\% \\
   jemalloc & 66\% & 66\% & 0\% & 0\% \\
   \hline
   \end{tabular}
   \caption{result-opera-google-space}
   \label{table:result-opera-google-space}
   \end{table}


libreoffice
=============
.. figure:: allocstats/result-soffice.png
   :scale: 50%
   
   :label:`soffice` LibreOffice results. Poor performance of jemalloc.

.. raw:: latex

   \begin{table}
   \begin{tabular}{r | l c c c}
   \hline
   \multicolumn{5}{c}{\bf Speed} \\
   \hline
   {\bf Driver} & {\bf Penalty (count)} & {\bf Penalty (weighted)} & {\bf Best} & {\bf Worst} \\
   \hline
   dlmalloc & 22\% & 0\% & 100\% & 5\% \\
   rmmalloc & 24\% & 1\% & 0\% & 6\% \\
   tcmalloc & 38\% & 3\% & 0\% & 15\% \\
   jemalloc & 65\% & 7\% & 0\% & 72\% \\
   \hline
   \end{tabular}
   \caption{result-soffice-speed}
   \label{table:result-soffice-speed}
   \end{table}


.. raw:: latex

   \begin{table}
   \begin{tabular}{r | l c c c}
   \hline
   \multicolumn{5}{c}{\bf Space} \\
   \hline
   {\bf Driver} & {\bf Penalty (count)} & {\bf Penalty (weighted)} & {\bf Best} & {\bf Worst} \\
   \hline
   tcmalloc & 0\% & 0\% & 100\% & 0\% \\
   dlmalloc & 29\% & 1\% & 0\% & 100\% \\
   rmmalloc & 45\% & 12\% & 0\% & 0\% \\
   jemalloc & 75\% & 70\% & 0\% & 0\% \\
   \hline
   \end{tabular}
   \caption{result-soffice-space}
   \label{table:result-soffice-space}
   \end{table}

.. See table :ref:`table:result-opera-google-space` for blarf.
.. See table :ref:`table:result-opera-google-speed` for glorf.

sqlite
=============

zip
============

ls
===============

cfrac
===============

