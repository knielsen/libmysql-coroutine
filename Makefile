all: sync-example1 swapcontext-example

sync-example1: sync-example1.c
	gcc -o sync-example1 sync-example1.c -lmysqlclient_r

swapcontext-example: swapcontext-example.c my_context.c my_context.h
	gcc -o swapcontext-example swapcontext-example.c my_context.c
