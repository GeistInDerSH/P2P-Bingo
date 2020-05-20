CC = g++
DEBUG_FLAGS = -g -O0 -DDEBUG -pthread
CFLAGS = $(DEBUG_FLAGS) -Wall
RM = rm -f

all: server client

server: server.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

client: client.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	$(RM) *.o server client
