.PHONY: tests

all: compact.o listsort.o

SOURCES=listsort.c compact.c
CFLAGS=-std=gnu99

.c.o:
	g++ -c $(CFLAGS) $<

tests: test_*.cpp $(SOURCES)
	gcc -o run_tests test_*.cpp $(SOURCES) -g -std=gnu99 -lstdc++ -lgtest -lgtest_main -lpthread -lrt

compact: $(SOURCES)
	gcc -o compact $(SOURCES)

clean:
	rm -rf listsort.o compact.o run_tests compact
