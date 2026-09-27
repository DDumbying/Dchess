# Opening Book Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The built-in engine plays from a Polyglot or built-in book, and dchess names the opening being played.

**Architecture:** `game/book.c` (keys, file book, built-in book, pick, names) and `game/openings.c` (line data) are core code with unit tests. The built-in driver consults the book before searching. The TUI, CLI and profiles select the book, and the UI and PGN show the opening name.

**Tech Stack:** C, ncursesw, `make test`.

**Spec:** `docs/superpowers/specs/2026-09-27-opening-book-design.md`

> Terse by request. The spec holds the API and all values.

## Global Constraints

- Zero warnings under `-O2 -Wall`. Use `make -B` after a header change.
- Core never includes ncurses. Sparse comments. No attribution trailers.
- The Random64 table comes verbatim from a published Polyglot source (python-chess `polyglot.py` or the Polyglot `book_format.html`). Any mismatch is caught by the test keys.
- tmux: use `-s dchess-t` and exact targets `'=dchess-t:'` only, with a scratch HOME and XDG.

## Review Focus

1. **The en-passant rule in keys:** an ep square counts only if a pawn can capture onto it. Test: Task 1, the vector `e2e4 d7d5 e4e5 f7f5`, and a position with an ep square but no capturer.
2. **A huge or sparse book file:** refused over 512 MiB, without reading it. Test: Task 2, a sparse 600 MiB file made with `ftruncate`.
3. **A book entry whose move is illegal** in the position (corruption or a key collision) is skipped. Test: Task 2.
4. **FEN games and undo:** a FEN game gets no opening name; after `undo` the book resumes. Test: Task 2 (names); Task 4 (tmux undo).
5. **`go` on a human's turn** also uses the book: the `go` driver gets the same book. Checked with tmux in Task 4.

---

### Task 1: Polyglot keys

**Files:** `headers/game/book.h`, `src/game/book.c`, `tests/test_book.c`

- [ ] **Step 1:** Header with the spec's API.
- [ ] **Step 2: Test.** `book_key` for the spec's five vectors, plus `e2e4 d7d5 e4e5 f7f5 e1e2` = `00fdd303c946bdd9` and `e2e4 d7d5 e4e5 f7f5 e1e2 e8f7` = `652a607ca3f242c1`. Also: after `d2d4 … ` an ep square with no capturer gives the same key as without it.
- [ ] **Step 3:** `make build/test_book` → undefined `book_key`.
- [ ] **Step 4: Implement.**
  - Fetch the 781-value table (WebFetch the raw python-chess `polyglot.py`, `POLYGLOT_RANDOM_ARRAY`) into `static const U64 RANDOM64[781]`.
  - Key = pieces `64*kind + 8*row + file` with kind `bp=0 wp=1 bn=2 wn=3 … bk=10 wk=11`, XOR castling `768+{0..3}` (K Q k q), ep `772+file` only when a side-to-move pawn attacks the ep square, and XOR `780` when White is to move.
  - The remaining API returns 0/NULL for now.
- [ ] **Step 5:** Tests pass; commit `feat: Polyglot position keys`.

### Task 2: Books, picking, names

**Files:** `src/game/book.c`, `headers/game/openings.h`, `src/game/openings.c`, `tests/test_book.c`

- [ ] **Step 1: Tests.**
  - **File book:** the test writes a `.bin` of 3 entries for the start position (e2e4 weight 3, d2d4 weight 1, and an illegal move), plus a castling entry `e1h1` for a position after `e2e4 e7e5 g1f3 b8c6 f1c4 g8f6` and a promotion entry for `8/P6k/8/8/8/8/8/K7 w`.
    - The seeded pick returns only legal moves, and e2e4 about 3× as often as d2d4 over 400 picks.
    - Castling maps to dchess's O-O move; the promotion maps to a7a8q.
    - A missing file, a size not divisible by 16, and a sparse 600 MiB file each give NULL with a message.
  - **Built-in book:**
    - every line in `OPENINGS` replays legally;
    - `book_opening` after each full line returns that line's ECO and name;
    - 1.Nf3 d5 2.d4 (a transposition) names the same as 1.d4 d5 2.Nf3 when both are lines;
    - after an off-book move the deepest earlier name is kept;
    - a FEN start gives NULL.
  - **Limits:** at ply 8, Easy returns 0; at ply 15 Medium can still pick when in book; at ply 16 it returns 0.
- [ ] **Step 2:** Watch them fail.
- [ ] **Step 3: `openings.c`** has about 150 mainstream lines: A00–E99 staples, including the Sicilian variants, Ruy Lopez, Italian, French, Caro-Kann, QGD/QGA, Slav, KID, Nimzo, Grünfeld, English, Réti, London, Scandinavian, Pirc, Dutch and more. Each line is 6–14 plies in UCI notation.
- [ ] **Step 4: Implement.**
  - `book_open`: stat, reject over 512 MiB or a size % 16 ≠ 0, then read the file and keep the big-endian entries.
  - `book_builtin`: replay the lines, count `(key, move)` pairs into a sorted array, and store the name entries.
  - `book_pick`: binary search to the first entry for the key; gather the legal decoded moves; draw by weight with an `rng` LCG.
  - `book_opening`: replay the game from the start (return NULL if `start_fen[0]`); the deepest ply whose key is a name entry wins.
- [ ] **Step 5:** Tests pass, then run `make -B test`. Commit `feat: built-in and Polyglot books, opening names`.

### Task 3: The engine plays book moves

**Files:** `src/game/opponent.c`, `headers/game/opponent.h`, `tests/test_opponent.c`, and the `opponent_builtin` callers (`src/tui/commands.c`)

- [ ] **Step 1: Test.** `opponent_builtin(depth, ms, book, level)` with the built-in book at the start position → poll within 50 ms returns a legal move with `depth_reached` 0. With a NULL book → a normal search.
- [ ] **Step 2:** Watch it fail (signature).
- [ ] **Step 3: Implement.**
  - The driver stores `book` and `level`, and keeps an `rng` seeded from time.
  - In `start`, when `book_pick` gives a move: set the result, `ready = 1`, `threaded = 0`, and start no thread.
  - Update the callers: pass `state->book` and the player's level. The go driver uses Medium's level.
- [ ] **Step 4:** Tests pass, then run `make -B test`. Commit `feat: engine plays from the opening book`.

### Task 4: Selecting the book, showing the opening

**Files:** `headers/tui/tui.h`, `src/tui/tui.c`, `src/tui/commands.c`, `src/tui/panels.c`, `src/utils/cli.c` + `.h`, `src/game/profiles.c` + `.h`, `src/game/records.c` + `.h`, `README.md`; tests `test_cli.c`, `test_profiles.c`, `test_records.c`

- [ ] **Step 1: Tests.**
  - CLI `--book off`, `--book builtin` and `--book /x.bin` → `args.book`;
  - profiles `book =` round-trips;
  - `records_append` writes `ECO`/`Opening` when the game follows a named line, and `records_load` fills `eco`/`opening`.
- [ ] **Step 2:** Watch them fail.
- [ ] **Step 3: Implement.**
  - `TUIState.book` (`Book *`) and `book_choice[256]`; load in `tui_attach_players`, free in release.
  - A bad path → status message, then the built-in book.
  - The `book <builtin|off|path>` command reattaches the drivers.
  - `tui_remember_setup` saves `book`.
  - The engine panel shows `book` when `last_search.depth_reached == 0 && nodes == 0`.
  - One line under the moves panel: `B90 Sicilian Defence · Najdorf`.
  - PGN export and records get `ECO`/`Opening` tags via `book_opening`.
  - Update the help text and README.
- [ ] **Step 4:** Build clean, then run `make -B test`.
- [ ] **Step 5: tmux:**
  - 1.e4 c5 2.Nf3 d6 names a Sicilian;
  - the engine replies instantly with `book` in the panel;
  - `--book off` → it searches from move 1;
  - `--book /nonexistent` → a message, and the book is still used;
  - `undo` back into the book → it resumes;
  - `go` on your own turn plays a book move.
- [ ] **Step 6:** Commit `feat(tui): choose the book and show the opening name`.
