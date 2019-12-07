cc = gcc
CFLAGS := -Wall -g


all: guess_srv guess_cli

guess_srv: guess_srv.o guess_utils.o

guess_cli: guess_cli.o guess_utils.o

clean:
	rm -f *.o guess_srv guess_cli
