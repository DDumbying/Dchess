CC      = gcc
CFLAGS  = -Iheaders -O2 -Wall -pthread -D_XOPEN_SOURCE=600 $(shell ncursesw6-config --cflags 2>/dev/null || ncursesw5-config --cflags 2>/dev/null || echo "")
LDFLAGS = -lncursesw -pthread

SRC     = $(shell find src -name "*.c")
TARGET  = dchess

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
