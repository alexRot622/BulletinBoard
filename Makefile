COMMON=utils.c
CSRC=client.c
CFLAGS=-Wall -O3
CC=gcc
CPPSRC=server.cpp
CPPFLAGS=-Wall -O3
CPPC=g++
OBJS=utils.o

all: subscriber server

subscriber: $(COMMON) $(CSRC)
	$(CC) $(CFLAGS) -c $(COMMON)
	$(CC) -o subscriber $(CFLAGS) $(CSRC) $(OBJS)

server: $(COMMON) $(CPPSRC)
	$(CPPC) $(CFLAGS) -c $(COMMON)
	$(CPPC) -o server $(CPPFLAGS) $(CPPSRC) $(OBJS)

clean:
	rm subscriber server $(OBJS)
