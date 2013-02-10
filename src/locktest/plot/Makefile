.PHONY: clean

all: plot_optimal plot_dlmalloc

SOURCES=plot.cpp plot.h #../../compact/compact.c ../../compact/listsort.c
plot_dlmalloc: plot_dlmalloc.cpp dlmalloc.c $(SOURCES)
	g++ -g -o plot_dlmalloc plot_dlmalloc.cpp dlmalloc.c $(SOURCES) -fpermissive -lstdc++ -lrt

plot_cmalloc: plot_cmalloc.cpp dlmalloc.c $(SOURCES)
	g++ -g -o plot_cmalloc plot_cmalloc.cpp dlmalloc.c $(SOURCES) -fpermissive -lstdc++ -lrt

plot_optimal: plot_optimal.cpp $(SOURCES)
	g++ -g -o plot_optimal plot_optimal.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

clean:
	rm -rf plot_cmalloc plot_dlmalloc plot_optimal

