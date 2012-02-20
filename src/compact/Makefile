.PHONY: tests

all:  tests

SOURCES=listsort.c

tests: test_*.cpp $(SOURCES)
	gcc -o run_tests test_*.cpp $(SOURCES) -g -std=gnu99 -lstdc++ -lgtest -lgtest_main -lpthread

compact: $(SOURCES)
	gcc -o compact $(SOURCES)

