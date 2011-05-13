all: sync-example1 swapcontext-example gcc_amd64_example

sync-example1: sync-example1.c
	gcc -o sync-example1 sync-example1.c -lmysqlclient_r

swapcontext-example: swapcontext-example.c my_context.c my_context.h
	gcc -DUSE_UCONTEXT -o swapcontext-example swapcontext-example.c my_context.c

gcc_amd64_example: swapcontext-example.c my_context_amd64_gcc.c my_context.h
	gcc -DUSE_GCC_AMD64 -o gcc_amd64_example swapcontext-example.c my_context_amd64_gcc.c
