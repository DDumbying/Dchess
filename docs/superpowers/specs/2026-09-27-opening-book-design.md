# Opening book

**Status:** approved design, 2026-09-27
**Branch:** `feat/book`

## Goal

The built-in engine plays opening moves from a book: a built-in one, or any
Polyglot `.bin` the player supplies. dchess also names the opening being
played.

This is project E of five.

## Core: `src/game/book.c`

**Keys.** A position's key is the standard Polyglot key. It uses the 781-entry
Random64 table from the Polyglot format documentation, and follows its rules
for castling and en-passant.

**File book.**
- A `.bin` is read whole into memory: 16-byte entries, big-endian, sorted by
  key, each holding `key u64`, `move u16`, `weight u16` and `learn u32`.
- Lookup is a binary search on the key.
- Move decoding follows the format:
  - castling is written king-takes-rook (`e1h1`) and maps to dchess's
    castling move;
  - promotion is written as a piece code.
- A file whose size is not a multiple of 16 or that fails to open is rejected
  with a message, and the built-in book is used instead.

**Built-in book.**
- `src/game/openings.c` compiles in about 150 named main lines, each as
  `{ "B90", "Sicilian Defence · Najdorf", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6" }`.
- At load, each line is played from the start position. Every position along
  it gets an entry `(key, move)`, whose weight is the number of lines that
  make that move from that position.
- The final position of each line is recorded as a name entry
  `(key, eco, name)`.

**API.**

```c
typedef struct Book Book;
Book *book_builtin(void);
Book *book_open(const char *path, char *err, size_t n);   /* NULL + message on failure */
void  book_free(Book *b);
U64   book_key(const Position *pos);                        /* Polyglot key */
/* 0 when out of book or past the level's limit. rng: caller's state. */
Move  book_pick(const Book *b, const GameState *g, int level, unsigned *rng);
/* Deepest named position reached in the game's history, or NULL. */
const char *book_opening(const GameState *g, const char **eco);
```

- **Depth limits:**
  - Easy picks only while `move_count - log_start < 8`;
  - Medium while it is under 16;
  - Hard has no limit.
- **Picking:** among the entries for the position, one is chosen at random
  with probability in proportion to its weight. Entries whose move is not
  legal in the position are skipped.
- **Names:** always come from the built-in line list, whichever book is in
  use. A game that started from a FEN has no opening name.

## Integration

- **Engine:** `opponent_builtin` takes a `Book *`, which may be NULL. On
  `start`, if `book_pick` returns a move, the driver finishes at once with
  `best_move` set and `depth_reached = 0`, and no search runs. The engine
  panel shows `book` when a result has depth 0.
- **Which book:**
  - `--book <builtin|off|path>` for one run;
  - the in-game `book <builtin|off|path>` command;
  - the profile's `book =` line in `profiles.conf`, remembered on game start
    like the theme.
  - The default is `builtin`.
- **Opening name:**
  - shown on one line under the moves panel;
  - PGN export and `games.pgn` records gain `ECO` and `Opening` tags when a
    name is known;
  - `Record` gains `eco[4]` and `opening[64]`, so project D can use them
    later.
- **UCI engines** are unaffected.

## Testing

- **`tests/test_book.c`:**
  - Polyglot keys for the published test positions:
    - the start position, `463b96181691fc9c`;
    - after `e2e4`, `823c9b50fd114196`;
    - after `e2e4 d7d5`, `0756b94461c50fb0`;
    - after `e2e4 d7d5 e4e5`, `662fafb965db29d4`;
    - after `e2e4 d7d5 e4e5 f7f5`, `22a48b5a8e47ff78`.
  - A `.bin` written by the test covers lookup, weighted choice (seeded rng),
    the castling-move mapping, a promotion, a missing file and a bad size.
  - Every built-in line is legal and its final position names correctly.
  - Names for a transposed move order, and after leaving the book.
  - The Easy and Medium depth limits.
  - `book_pick` returns 0 out of book.
- **Opponent test:** through `opponent_builtin` with the built-in book, the
  start position returns a book move at once, with `depth_reached` 0.
- **tmux:**
  - 1.e4 c5 2.Nf3 d6 shows `B50 Sicilian Defence` or deeper;
  - `--book off` makes the engine search from move 1;
  - a bad `--book` path shows a message and falls back to the built-in book.

## Out of scope

- Book learning or editing.
- A book for UCI engines.
- Opening statistics.
