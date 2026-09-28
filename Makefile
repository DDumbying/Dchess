CC      = gcc
CFLAGS_EXTRA ?=
CFLAGS  = -Iheaders -O2 -Wall $(CFLAGS_EXTRA) -pthread -D_XOPEN_SOURCE=600 $(shell ncursesw6-config --cflags 2>/dev/null || ncursesw5-config --cflags 2>/dev/null || echo "")
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

test: $(TARGET) build/fake_uci build/genfens $(TEST_BIN)
	@for t in $(TEST_BIN); do \
		echo "── $$t ──"; \
		./$$t || exit 1; \
		echo; \
	done
	@for t in tests/test_manpage.sh tests/test_install.sh; do \
		echo "── $$t ──"; \
		sh $$t || exit 1; \
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

build/mkpuzzles: tools/mkpuzzles.c $(CORE_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) $< $(CORE_SRC) -o $@ $(LDFLAGS)

# The bundled puzzle set, from the Lichess puzzle CSV (database.lichess.org)
puzzles: build/mkpuzzles
	./build/mkpuzzles --csv $(CSV) --out src/game/puzzles_data.c

build/genfens: tools/genfens.c $(CORE_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) $< $(CORE_SRC) -o $@ $(LDFLAGS)

# Labelled positions from Stockfish self-play (GAMES in total, JOBS at once)
GAMES ?= 5000
JOBS  ?= 14
genfens: build/genfens
	@rm -f build/fens-*.txt; pids=""; for j in $$(seq 1 $(JOBS)); do ./build/genfens --games $$(( ($(GAMES) + $(JOBS) - 1) / $(JOBS) )) --seed $$j --out build/fens-$$j.txt & pids="$$pids $$!"; done; \
	fail=0; for p in $$pids; do wait $$p || fail=1; done; [ $$fail = 0 ] || { echo "genfens: a job failed"; exit 1; }
	@cat build/fens-*.txt > build/fens.txt && rm -f build/fens-*.txt && wc -l build/fens.txt

bench: build/bench
	./build/bench $(DEPTH)

clean:
	rm -f $(TARGET)
	rm -rf build

# ── Install ─────────────────────────────────────────────────────────────
PREFIX  ?= /usr/local
DESTDIR ?=
VERSION  = $(shell sed -n 's/.*DCHESS_VERSION "\(.*\)".*/\1/p' headers/utils/version.h)
SHARE    = $(DESTDIR)$(PREFIX)/share

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/dchess
	install -d $(SHARE)/man/man6
	sed 's/@VERSION@/$(VERSION)/' docs/dchess.6 > $(SHARE)/man/man6/dchess.6
	chmod 644 $(SHARE)/man/man6/dchess.6
	install -Dm644 README.md $(SHARE)/doc/dchess/README.md
	install -Dm644 docs/guide.md $(SHARE)/doc/dchess/guide.md
	install -Dm644 CHANGELOG.md $(SHARE)/doc/dchess/CHANGELOG.md
	install -Dm644 LICENSE $(SHARE)/licenses/dchess/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dchess $(SHARE)/man/man6/dchess.6
	rm -f $(SHARE)/doc/dchess/README.md $(SHARE)/doc/dchess/guide.md $(SHARE)/doc/dchess/CHANGELOG.md
	rm -f $(SHARE)/licenses/dchess/LICENSE
	rmdir $(SHARE)/doc/dchess $(SHARE)/licenses/dchess 2>/dev/null || true

.PHONY: all test bench match genfens tune puzzles install uninstall clean
