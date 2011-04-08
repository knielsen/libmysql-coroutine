all: sync-example1

sync-example1: sync-example1.c
	gcc -o sync-example1 sync-example1.c -lmysqlclient_r
