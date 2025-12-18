CC = gcc
CFLAGS = -std=c99 \
         -D_POSIX_C_SOURCE=200809L \
         -D_XOPEN_SOURCE=700 \
         -Wall -Wextra -Werror \
         -Wno-unused-parameter \
         -fno-asm \
         -Iinclude

SRCDIR = src
TARGET = shell.out
HEADERS = $(wildcard include/*.h)

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/prompt.c \
       $(SRCDIR)/parser.c \
       $(SRCDIR)/builtins.c \
       $(SRCDIR)/executor.c \
       $(SRCDIR)/signals.c \
       $(SRCDIR)/jobs.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# every .o depends on ALL headers so struct changes trigger recompile
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)

.PHONY: all clean
