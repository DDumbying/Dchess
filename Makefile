CC      = gcc
CFLAGS  = -Iheaders -O2 -Wall -Wno-unused-function
LDFLAGS = -lncursesw

SRC     = $(shell find src -name "*.c")
TARGET  = dchess

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
