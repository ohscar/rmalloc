#!/bin/bash

#valgrind --tool=lackey --trace-mem=yes $1 --gtest_color=yes --gtest_filter=AllocTest.CompactLock
valgrind --tool=memcheck $1 --gtest_color=yes --gtest_filter=AllocTest.CompactLock

