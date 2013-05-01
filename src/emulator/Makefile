.PHONY: clean

all: plot_optimal plot_dlmalloc

SOURCES=plot.cpp plot.h #../../compact/compact.c ../../compact/listsort.c

CFLAGS=-g -O0 -Wshadow

.cpp.o:
	g++ -c $(CFLAGS) $<

.c.o:
	g++ -c $(CFLAGS) $<

plot_dlmalloc: plot_dlmalloc.o dlmalloc.o plot.o
	g++ -o plot_dlmalloc plot_dlmalloc.o dlmalloc.o plot.o

plot_cmalloc: plot_cmalloc.cpp dlmalloc.c $(SOURCES)
	g++ -g -O0 -o plot_cmalloc plot_cmalloc.cpp dlmalloc.c $(SOURCES) -fpermissive -lstdc++ -lrt

plot_optimal: plot_optimal.cpp $(SOURCES)
	g++ -g -O0 -o plot_optimal plot_optimal.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

clean:
	rm -rf plot_cmalloc plot_dlmalloc plot_optimal *.o

