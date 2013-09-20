.PHONY: clean

RMALLOC_BINARY=plot_rmalloc

CFLAGS=-std=gnu++11 -I.. -I../compact

ifneq ($(COMPACTING),)
	CFLAGS += -DCOMPACTING
	RMALLOC_BINARY=plot_rmalloc_compacting
endif

#all: plot_optimal plot_dlmalloc plot_rmalloc
all: $(RMALLOC_BINARY) plot_dlmalloc
all: CFLAGS += -O3 -march=core2

debug: CFLAGS += -g -DDEBUG # -O0
debug: plot_dlmalloc $(RMALLOC_BINARY)

COMPACT_OBJS=../compact/compact.o ../compact/listsort.o
SOURCES=plot.cpp plot.h #../../compact/compact.c ../../compact/listsort.c

CFLAGS=-I.. -I../compact # -std=gnu++11

#CFLAGS+=-O3 -march=core2

WARNFLAGS=-Wshadow

CFLAGS += $(WARNFLAGS)  -std=gnu++0x


#../compact/compact.o: ../compact/compact.c
#	make -C ../compact

.cpp.o:
	g++ -c $(CFLAGS) $<

.c.o:
	g++ -c $(CFLAGS) $<

$(RMALLOC_BINARY): plot_rmalloc.o plot.o $(COMPACT_OBJS) 
	g++ -o $(RMALLOC_BINARY) plot_rmalloc.o plot.o $(COMPACT_OBJS) -lrt

plot_dlmalloc: plot_dlmalloc.o dlmalloc.o plot.o
	g++ -o plot_dlmalloc plot_dlmalloc.o dlmalloc.o plot.o  -lrt

plot_cmalloc: plot_cmalloc.cpp dlmalloc.c $(SOURCES)
	g++ -g -O0 -o plot_cmalloc plot_cmalloc.cpp dlmalloc.c $(SOURCES) -fpermissive -lstdc++ -lrt

plot_optimal: plot_optimal.cpp $(SOURCES)
	g++ -g -O0 -o plot_optimal plot_optimal.cpp $(SOURCES) -fpermissive -lstdc++ -lrt

clean:
	rm -rf plot_rmalloc plot_rmalloc_compacting plot_dlmalloc plot_optimal *.o

settings:
	@echo COMPACTING=1 for automatic compacting after free().
