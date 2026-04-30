CC = gcc
CFLAGS = -Iheaders -O2

SRC = $(shell find src -name "*.c")

all:
	$(CC) $(CFLAGS) $(SRC) -o engine
