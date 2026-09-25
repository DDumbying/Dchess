CC      = gcc
CFLAGS  = -Iheaders -O2 -Wall -pthread -D_XOPEN_SOURCE=600 $(shell ncursesw6-config --cflags 2>/dev/null || ncursesw5-config --cflags 2>/dev/null || echo "")
LDFLAGS = -lncursesw -pthread -lm

SRC     = $(shell find src -name "*.c")
TARGET  = dchess

# Everything except the TUI: the engine, the game rules and the shared
# utilities. The tests link against this set -- they must never need
# ncurses or a terminal to run.
CORE_SRC = $(shell find src/engine src/game src/utils -name "*.c")

TEST_SRC  = $(wildcard tests/test_*.c) tests/perft.c
TEST_BIN  = $(patsubst tests/%.c,build/%,$(TEST_SRC))

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

# ── Tests ───────────────────────────────────────────────────────────────
# `make test` builds and runs every suite, stopping at the first failure
# (each one exits non-zero when a check fails). perft is the slowest by
# far, so it runs last.
build:
	@mkdir -p build

build/%: tests/%.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) -Itests $< $(CORE_SRC) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do \
		echo "── $$t ──"; \
		./$$t || exit 1; \
		echo; \
	done
	@echo "All suites passed."

# Not part of `make test`: it measures, it does not pass or fail.
bench: build/bench
	./build/bench $(DEPTH)

clean:
	rm -f $(TARGET)
	rm -rf build

.PHONY: all test bench clean
