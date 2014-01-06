.PHONY: tests

all: compact.o listsort.o
all: CFLAGS += -O3 -march=core2

debugcode: compact.o listsort.o
debugcode: CFLAGS += -g -O0 -DDEBUG

debug: compact.o listsort.o
debug: CFLAGS += -g -O0

profile: compact.o listsort.o
profile: PROFILING=-pg -g3
#profile: CFLAGS += -O3 -march=core2

SOURCES=listsort.c compact.c
CFLAGS=-std=gnu99

.c.o:
	g++ $(PROFILING) -c $(CFLAGS) $<

tests: test_*.cpp $(SOURCES)
	gcc -o run_tests test_*.cpp $(SOURCES) -g -std=gnu99 -lstdc++ -lgtest -lgtest_main -lpthread -lrt

compact: $(SOURCES)
	gcc -o compact $(SOURCES)

clean:
	rm -rf *.o run_tests compact
