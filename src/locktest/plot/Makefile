.PHONY: clean

all: plot_optimal

SOURCES=plot.cpp plot.h #../../compact/compact.c ../../compact/listsort.c
plot_cmalloc: plot_cmalloc.cpp $(SOURCES)
	g++ -g -o plot_cmalloc plot_cmalloc.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

plot_optimal: plot_optimal.cpp $(SOURCES)
	g++ -g -o plot_optimal plot_optimal.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

clean:
	rm -rf plot_cmalloc
