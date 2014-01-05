
PHONY: all

#RMALLOC_BINARY=plot_rmalloc

CFLAGS=-std=gnu++11 -I../../jeff -I.. -I../..

#ifneq ($(DEBUG),)
#	CFLAGS += -DDEBUG
#endif

#all: plot_optimal plot_dlmalloc plot_rmalloc
all: plot_rmalloc_compacting plot_rmalloc plot_dlmalloc 
all: CFLAGS += -O3 -march=core2

debug: plot_rmalloc_compacting plot_rmalloc plot_dlmalloc 
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
	g++ $(PROFILING) -c $(CFLAGS) -o $@ $<

.c.o:
	g++ -o $@ $(PROFILING) -c $(CFLAGS) $<

#
# bleh, this is not a Makefile...
# 
# warning, big mess ahead.  and above.
#
plot.o: ../plot.cpp
	g++ $(PROFILING) -c $(CFLAGS) $<

plot_rmalloc_compacting.o: CFLAGS += -DCOMPACTING
plot_rmalloc_compacting.o: plot_rmalloc.cpp
	g++ $(PROFILING) -c $(CFLAGS) -o $@ $<

dlmalloc.o: ../dlmalloc.c
	g++ $(PROFILING) -c $(CFLAGS) $<

plot_rmalloc: plot_rmalloc.o plot.o $(COMPACT_OBJS) 
	g++ $(PROFILING) -o plot_rmalloc plot.o plot_rmalloc.o $(COMPACT_OBJS)

plot_rmalloc_compacting: plot_rmalloc_compacting.o plot.o $(COMPACT_OBJS) 
	g++ $(PROFILING) -o plot_rmalloc_compacting plot.o plot_rmalloc_compacting.o $(COMPACT_OBJS)

plot_dlmalloc: plot_dlmalloc.o dlmalloc.o plot.o
	g++ $(PROFILING) -o plot_dlmalloc plot_dlmalloc.o dlmalloc.o plot.o

plot_cmalloc: plot_cmalloc.cpp dlmalloc.c $(SOURCES)
	g++ -g -O0 -o plot_cmalloc plot_cmalloc.cpp dlmalloc.c $(SOURCES) -fpermissive -lstdc++ -lrt

plot_optimal: plot_optimal.cpp $(SOURCES)
	g++ -g -O0 -o plot_optimal plot_optimal.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

clean:
	rm -rf plot_rmalloc plot_rmalloc_compacting plot_dlmalloc plot_optimal *.o

settings:
	@echo COMPACTING=1 for automatic compacting after free().
