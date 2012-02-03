.PHONY: tests

all:  tests

SOURCES=compact.c

tests: test_*.cpp $(SOURCES)
	gcc -o run_tests test_*.cpp -std=gnu99 -lstdc++ -lgtest -lgtest_main -lpthread

compact: $(SOURCES)
	gcc -o compact $(SOURCES)

