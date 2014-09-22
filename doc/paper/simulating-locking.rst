.. raw:: latex

    \chapter{Simulating Application Runtime
        \label{chapter-simulating-application-runtime}}

This is done for Steve, because...

XXX: expand on why this is done! what is its relevance? connect to earlier discussion on how error-prone it is to rewrite
application. ("as discussed in chapter :ref:`jeff....`, ...")

Gathering Memory Access Data
==================================
Simply getting malloc/free calls is trivially done by writing a malloc wrapper and make use of Linux' ``LD_PRELOAD``
technique for preloading a shared library, to make the applications use our own allocator that can do logging, instead
of the system allocator. Unfortunately that is not enough. To get statistics on
memory *access* patterns one needs to essentially simulate the system the application runs in.  Options considered were
TEMU [#]_ from the BitBlaze [#]_ project, but because it would not build on my Linux system, I looked at Valgrind [#]_
before trying again. Turns out Valgrind has good instrumentation support in their tools. Valgrind was originally a tool
for detecting memory leaks in applications on the x86 platform via emulation and has since evolved to support more
hardware platforms and providing tools for doing other instrumentation tasks. Provided with Valgrind an example tool
*Lackey* does parts of what I was looking for but missing other parts. I instead ended up patching the *memcheck* tool
[#]_ to capture load/store/access operations and logging them to file, if they were in the boundaries of lowest address
allocated to highest address allocated. This will still give false positives when there are holes (lowest is only
decreased and highest is only increased) but reduces the logging output somewhat. Memory access can then be analyzed
offline. Note that it will only work for applications that use the system-malloc/free. Any applications using custom
allocators must be modified to use the system allocator, which generally means changing a setting in the source code and
recompiling.

.. [#] http://bitblaze.cs.berkeley.edu/temu.html
.. [#] http://bitblaze.cs.berkeley.edu/ 
.. [#] http://valgrind.org
.. [#] https://github.com/mikaelj/rmalloc/commit/a64ab55d9277492a936d7d7acfb0a3416c098e81 (2014-02-09: "valgrind-3.9.0: memcheck patches")

I've written a convenience script for this purpose and then optionally creates locking data::

    #!/bin/bash

    if [[ ! -d "$theapp" ]]; then
        mkdir $theapp
    fi
    echo "$*" >> ${theapp}/${theapp}-commandline
    ../../valgrind/vg-in-place --tool=memcheck $* 2>&1 > \
        /dev/null | grep '^>>>' > ${theapp}/${theapp}

    python -u ../steve/memtrace-to-ops/translate-memtrace-to-ops.py \
        ${theapp}/${theapp}

    python -u ../steve/memtrace-to-ops/translate-ops-to-locking-lifetime.py \
      ${theapp}/${theapp}

Beware that this takes long time for complex applications, about 30 minutes on an Intel Core i3-based system to load
http://www.google.com in the web browser Opera [#]_.

.. [#] http://www.opera.com

Simulating Locking
=====================
As described above,

XXX: Described where?

it is not a practical solution to rewrite applications. If there is a possible of creating an automated
method for finding approximate locking behaivour of applications, it should be investigated. 

A block with a lifetime close to the total number of operations has a long lifetime and therefore created in the
beginning of the application's lifetime.  The *macro* lifetime of a block is the relation between all ops within its
lifetime through the total ops count of the application.  A block with a small macro lifetime therefore is an object
that has a short life span, whereas a block with a large macro lifetime is an object with a large life span. Typically
a large value for macro lifetime means it's a global object and can be modelled thereafter.

Depnding on the relation between ops accessing the block in question and ops accessing other objects the access pattern
of the object can be modeled.  For example, if an object has 100 ops within its lifetime and 10 of them are its own
and 90 are others', the object would probably be locked at each access, whereas if it was the other way around, it is
more likely that the object is locked throughout its entire lifetime. Calculating lifetime requires a full opsfile,
including all access ops.


.. XXX: More on the specifics of lifetime calculation?

.. This will be expanded upon in Chapter :ref:`chapter-steve`.  XXX: make sure to expand on it!
