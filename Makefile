CC      = gcc
CFLAGS  = -Iheaders -O2 -Wall -pthread -D_XOPEN_SOURCE=600 $(shell ncursesw6-config --cflags 2>/dev/null || ncursesw5-config --cflags 2>/dev/null || echo "")
LDFLAGS = -lncursesw -pthread -lm

SRC     = $(shell find src -name "*.c")
TARGET  = dchess

# Everything except the TUI: the engine, the game rules and the shared
# utilities. The tests link against this set -- they must never need
# ncurses or a terminal to run.
CORE_SRC = $(shell find src/engine src/game src/utils -name "*.c")
HEADERS  = $(shell find headers src tools tests -name "*.h")

TEST_SRC  = $(wildcard tests/test_*.c) tests/perft.c
TEST_BIN  = $(patsubst tests/%.c,build/%,$(TEST_SRC))

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

# ── Tests ───────────────────────────────────────────────────────────────
# `make test` builds and runs every suite, stopping at the first failure
# (each one exits non-zero when a check fails). perft is the slowest by
# far, so it runs last.
build:
	@mkdir -p build

build/%: tests/%.c $(CORE_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) -Itests $< $(CORE_SRC) -o $@ $(LDFLAGS)

test: build/fake_uci $(TEST_BIN)
	@for t in $(TEST_BIN); do \
		echo "── $$t ──"; \
		./$$t || exit 1; \
		echo; \
	done
	@echo "All suites passed."

# Not part of `make test`: it measures, it does not pass or fail.
build/match: tools/match.c tools/tune_core.c $(CORE_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) -Itools $< tools/tune_core.c $(CORE_SRC) -o $@ $(LDFLAGS)

# Search options against each other, e.g. make match ARGS="--base none --cand pvs"
match: build/match
	./build/match $(ARGS)

build/test_tune: tests/test_tune.c tools/tune_core.c $(CORE_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) -Itools $< tools/tune_core.c $(CORE_SRC) -o $@ $(LDFLAGS)

build/tune: tools/tune.c tools/tune_core.c $(CORE_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) -Itools $< tools/tune_core.c $(CORE_SRC) -o $@ $(LDFLAGS)

# Tune the evaluation's added terms on build/fens.txt (make genfens first)
tune: build/tune
	./build/tune $(ARGS)

build/genfens: tools/genfens.c $(CORE_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) $< $(CORE_SRC) -o $@ $(LDFLAGS)

# Labelled positions from Stockfish self-play (GAMES in total, JOBS at once)
GAMES ?= 5000
JOBS  ?= 14
genfens: build/genfens
	@for j in $$(seq 1 $(JOBS)); do ./build/genfens --games $$(( $(GAMES) / $(JOBS) )) --seed $$j --out build/fens-$$j.txt & done; wait
	@cat build/fens-*.txt > build/fens.txt && rm -f build/fens-*.txt && wc -l build/fens.txt

bench: build/bench
	./build/bench $(DEPTH)

clean:
	rm -f $(TARGET)
	rm -rf build

.PHONY: all test bench match genfens tune clean
