all: main debug

main: main.c rmalloc.c
	gcc -o main main.c rmalloc.c

debug: main.c rmalloc.c
	gcc -g -o debug main.c rmalloc.c
