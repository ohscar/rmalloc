
PHONY: all

#RMALLOC_BINARY=plot_rmalloc

CFLAGS=-std=gnu++11 -I../../jeff -I.. -I../..

#ifneq ($(DEBUG),)
#	CFLAGS += -DDEBUG
#endif

#all: plot_optimal plot_dlmalloc plot_rmalloc
all: plot_rmalloc_compacting plot_rmalloc_compacting_maxmem plot_rmalloc plot_dlmalloc plot_jemalloc plot_tcmalloc
all: CFLAGS += -O3 -march=core2

debug: plot_rmalloc_compacting plot_rmalloc_compacting_maxmem plot_rmalloc plot_dlmalloc  plot_jemalloc plot_tcmalloc
debug: CFLAGS += -g -O0 -DDEBUG

profile: PROFILING=-pg -g3
#profile: CFLAGS += -O3 -march=core2
profile: plot_dlmalloc plot_rmalloc_compactingplot_rmalloc 


COMPACT_OBJS=../../jeff/compact.o ../../jeff/listsort.o
SOURCES=../plot.cpp ../plot.h #../../compact/compact.c ../../compact/listsort.c

#CFLAGS+=-O3 -march=core2

WARNFLAGS=-Wshadow

CFLAGS += $(WARNFLAGS)  -std=gnu++0x

.cpp.o:
	g++ $(PROFILING) $(CFLAGS) -o $@ -c $<

.c.o:
	g++ $(PROFILING) $(CFLAGS) -o $@ -c $<

../../jeff/compact_maxmem.o: CFLAGS += -DJEFF_MAX_RAM_VS_SLOWER_MALLOC=1
../../jeff/compact_maxmem.o: ../../jeff/compact.c
	g++ $(CFLAGS) -o $@ -c $<

#
# bleh, this is not a Makefile...
# 
# warning, big mess ahead.  and above.
#
plot.o: ../plot.cpp
	g++ $(PROFILING) $(CFLAGS) -o $@ -c $<

plot_rmalloc_compacting.o: CFLAGS += -DCOMPACTING
plot_rmalloc_compacting.o: plot_rmalloc.cpp
	g++ $(PROFILING) $(CFLAGS) -o $@ -c $<

dlmalloc.o: ../dlmalloc.c
	g++ $(PROFILING) $(CFLAGS) -o $@ -c $<

plot_rmalloc: plot_rmalloc.o plot.o $(COMPACT_OBJS) 
	g++ $(PROFILING) -o $@ $^

plot_rmalloc_compacting: plot_rmalloc_compacting.o plot.o $(COMPACT_OBJS) 
	g++ $(PROFILING) -o $@ $^

plot_rmalloc_compacting_maxmem: plot_rmalloc_compacting.o plot.o ../../jeff/listsort.o ../../jeff/compact_maxmem.o
	g++ $(PROFILING) -o $@ $^

plot_tcmalloc: plot_tcmalloc.o jemalloc/libjemalloc.so plot.o
	g++ $(PROFILING)  plot_tcmalloc.o  plot.o -Ltcmalloc  -ltcmalloc  -o plot_tcmalloc

plot_jemalloc: plot_jemalloc.o jemalloc/libjemalloc.so plot.o
	g++ $(PROFILING)  plot_jemalloc.o  plot.o -Ljemalloc  -ljemalloc  -o plot_jemalloc

plot_dlmalloc: plot_dlmalloc.o dlmalloc.o plot.o
	g++ $(PROFILING) -o plot_dlmalloc plot_dlmalloc.o dlmalloc.o plot.o

plot_cmalloc: plot_cmalloc.cpp dlmalloc.c $(SOURCES)
	g++ -g -O0 -o plot_cmalloc plot_cmalloc.cpp dlmalloc.c $(SOURCES) -fpermissive -lstdc++ -lrt

plot_optimal: plot_optimal.cpp $(SOURCES)
	g++ -g -O0 -o plot_optimal plot_optimal.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

clean:
	rm -rf plot_rmalloc plot_rmalloc_compacting plot_rmalloc_compacting_maxmem plot_dlmalloc plot_jemalloc plot_tcmalloc plot_optimal *.o

settings:
	@echo COMPACTING=1 for automatic compacting after free().
