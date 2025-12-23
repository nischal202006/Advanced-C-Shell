CC = gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
         -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function
LDFLAGS = -lcrypto

all: client server

client: client.c sham.h
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

server: server.c sham.h
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

clean:
	rm -f client server *.o *.txt

.PHONY: all clean
