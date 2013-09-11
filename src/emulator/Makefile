.PHONY: clean

#all: plot_optimal plot_dlmalloc plot_rmalloc
all: plot_dlmalloc plot_rmalloc

COMPACT_OBJS=../compact/compact.o ../compact/listsort.o
SOURCES=plot.cpp plot.h #../../compact/compact.c ../../compact/listsort.c

CFLAGS=-std=gnu++11 -I..

#FLAGS+=-g -O0
CFLAGS+=-O3 -march=core2

WARNFLAGS=-Wshadow

CFLAGS += $(WARNFLAGS)  -std=gnu++0x

../compact/compact.o: ../compact/compact.c
	make -C ../compact

.cpp.o:
	g++ -c $(CFLAGS) $<

.c.o:
	g++ -c $(CFLAGS) $<

plot_rmalloc: plot_rmalloc.o plot.o $(COMPACT_OBJS) 
	g++ -o plot_rmalloc plot_rmalloc.o plot.o $(COMPACT_OBJS)

plot_dlmalloc: plot_dlmalloc.o dlmalloc.o plot.o
	g++ -o plot_dlmalloc plot_dlmalloc.o dlmalloc.o plot.o

plot_cmalloc: plot_cmalloc.cpp dlmalloc.c $(SOURCES)
	g++ -g -O0 -o plot_cmalloc plot_cmalloc.cpp dlmalloc.c $(SOURCES) -fpermissive -lstdc++ -lrt

plot_optimal: plot_optimal.cpp $(SOURCES)
	g++ -g -O0 -o plot_optimal plot_optimal.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

clean:
	rm -rf plot_cmalloc plot_dlmalloc plot_optimal *.o

