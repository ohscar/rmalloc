.PHONY: tests

all: tests

SOURCES=buddy.c

tests: test_*.cpp $(SOURCES)
	gcc -o run_tests -std=gnu99 test_*.cpp $(SOURCES) -lstdc++ -lgtest -lgtest_main -lpthread

buddy: $(SOURCES)
	gcc -o buddy $(SOURCES)

