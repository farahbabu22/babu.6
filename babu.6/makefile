CC=gcc
CFLAGS=-Wall -g -pedantic -Wextra -Wcpp -std=c99 -D_POSIX_SOURCE

all: master user fifo_daemon

master: master.o oss.o process.h vm.h clock.h
	$(CC) $(CFLAGS) master.o oss.o -o master

user: user.o oss.o
	$(CC) $(CFLAGS) user.o oss.o -o user

fifo_daemon: fifo_daemon.c
	$(CC) $(CFLAGS) fifo_daemon.c oss.o -o fifo_daemon

clean:
	rm -rf master user fifo_daemon output.txt *.o *.log *.dSYM
