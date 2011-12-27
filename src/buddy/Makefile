.PHONY: tests

all: tests tests_int

SOURCES=buddy.c

tests: test_*.cpp $(SOURCES)
	gcc -o run_tests -std=gnu99 test_*.cpp $(SOURCES) -lstdc++ -lgtest -lgtest_main -lpthread

tests_int: testint_*.cpp 
	gcc -o run_testsint -g -std=gnu99 testint_*.cpp -lstdc++ -lgtest -lgtest_main -lpthread

buddy: $(SOURCES)
	gcc -o buddy $(SOURCES)

