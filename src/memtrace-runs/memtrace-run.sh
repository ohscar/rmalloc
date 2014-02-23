#!/bin/bash

theapp=$1

if [[ "$1" == "" ]]; then
    echo "Program plus parameters, e.g. $0 ls /usr/bin"
    exit
fi

if [[ ! -d "$1" ]]; then
    mkdir $1
fi

echo "$*" >> ${theapp}/${theapp}-commandline

echo "Getting memtrace..."

../../valgrind/vg-in-place --tool=memcheck $* 2>&1 > /dev/null | grep '^>>>' > ${theapp}/${theapp}

echo "Translating..."

python -u ../steve/memtrace-to-ops/translate-memtrace-to-ops.py ${theapp}/${theapp}

echo "Warning: The following step is very slow. (full lifetime calculation)"
echo -n "Do you want to proceed? [yN] "
read answer

echo "Proceeding..."

if [[ "$answer" == "y" ]]; then
    python -u ../steve/memtrace-to-ops/translate-ops-to-locking-lifetime.py ${theapp}/${theapp}
fi
