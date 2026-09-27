# Game replay

**Status:** approved design, 2026-09-27
**Branch:** `feat/replay` (from `origin/fix/polish` at `fe9312a`, which has #9 and #10)

## Goal

Step through any finished game, whether one of your own from `games.pgn` or
any PGN file, and continue play from any position in it.

## Core: `src/game/replay.c`, `headers/game/replay.h`

```c
typedef struct {
    char white[PLAYER_NAME_MAX + 1], black[PLAYER_NAME_MAX + 1];
    char result[8], date[11], event[64];
    long offset;                    /* of the game's first tag line */
} ReplayEntry;
typedef struct { ReplayEntry *e; int count, cap; } ReplayList;

typedef struct {
    ReplayEntry info;
    char fen[FEN_BUFSIZE];          /* start position; "" = standard */
    Move moves[MAX_MOVE_HISTORY];
    int  count;
    char err[96];                   /* "" or "stopped at move 23: Qxh9" */
} ReplayGame;

int  replay_list(const char *path, ReplayList *out, char *err, size_t n);  /* 0 on failure */
void replay_list_free(ReplayList *l);
int  replay_read(const char *path, long offset, ReplayGame *out, char *err, size_t n);
```

**Listing:**
- A game starts at a `[` line that follows movetext, or at the start of the
  file.
- The tags are unescaped as `records.c` does.
- Files over 64 MiB, or ones that are not regular files, are refused.
- The list stops at 10,000 games.
- `\r` before `\n` is ignored.

**Records:** `Record` gains `long offset`, filled by `records_load`, so the
lists of your own games can open them.

**Reading moves:**
- The start position comes from the `FEN` tag, or the standard position.
- Skipped in the movetext:
  - `{…}` comments, nested `(…)` variations and `;` line comments;
  - `$n` annotations and move numbers (`12.`, `12...`);
  - the result tokens `1-0 0-1 1/2-1/2 *`.
- Each other token is stripped of trailing `+#!?`, and `0-0`/`0-0-0` become
  `O-O`/`O-O-O`.
- The token is matched against every legal move's `san_write`, which is
  stripped the same way.
- A move that matches nothing, or matches more than one, stops the reading:
  the moves so far are kept and `err` is set.
- Stops at `MAX_MOVE_HISTORY` moves.

## TUI: replay mode on the game screen

**State:** `TUIState` gains
`ReplayGame *replay`, `int replay_ply`, `int replay_auto`, `int replay_from`.
- `replay_from` is where esc returns to: the launcher, the stats page or
  the picker.
- The replay is shown by rebuilding `state->game` from the start position
  and playing the first `replay_ply` moves.
- **→** calls `game_play`, **←** calls `game_undo`, and Home/End rebuild.

**Keys:**

| Key | Action |
|-----|--------|
| ← / h, → / l | step one move |
| Home / g, End / G | jump to the start or end |
| space | auto-play on/off: one move a second, using the input timeout; stops at the end |
| p | play from here |
| esc | back to where the replay was opened from |

All other game keys and commands are ignored in replay.

**Screen:**
- The command bar shows
  `REPLAY · 12/64 · White – Black · 1-0 · ←→ step  home/end  space auto  p play  esc back`,
  clipped with `mvw_fit`.
- When `err` is set, it is shown once on the status line.
- The moves panel highlights the current move.
- The opening line comes from `book_opening`, as it does now.
- The engine panel shows `replay`; the clocks show `—`.
- No driver is attached, and nothing is saved to `games.pgn`.

**Play from here:**
- The position at `replay_ply` becomes a new game from that FEN (the earlier
  moves are not carried over).
- It uses the active profile's remembered setup: players, book and theme.
- With no profile, it is Guest against dchess Medium.
- The replay is freed and play starts as normal.

## Opening a replay

- **Stats page:** the recent list gets a cursor (↑↓); ⏎ opens the game from
  `games.pgn`, and esc returns to the stats page.
- **Launcher:** Tab cycles profiles → new game → card. In the card, ↑↓
  selects a game and ⏎ opens it; esc returns to the launcher.
- **`dchess --replay FILE`:**
  - With one game, the replay opens directly and esc quits.
  - With more than one, a picker opens:
    - its rows are `#  White – Black  result  date  event`, clipped;
    - ↑↓ and PgUp/PgDn move, ⏎ opens, and esc quits;
    - esc from a replay returns to the picker.
  - A file that cannot be read prints the error and exits 1, before curses
    starts.
- Legacy records (no moves) cannot be opened; ⏎ on one shows `no moves saved`.

## Testing

**`tests/test_replay.c`:**
- A game written by `pgn_write` reads back to the same moves.
- Comments, nested variations, `;` comments and NAGs are skipped.
- `0-0` and `O-O`, promotion `e8=Q`, and check or mate suffixes are
  accepted.
- An illegal token stops the reading, and `err` names it.
- A `FEN`/`SetUp` game starts from its FEN.
- A three-game file lists three entries with offsets that `replay_read`
  opens.
- CRLF line endings are accepted.
- A 65 MiB sparse file is refused.
- Escaped names are unescaped in the list.

**`tests/test_records.c`:** `offset` points at each game's `[Event` line.

**`tests/test_cli.c`:** `--replay FILE` sets `args.replay`.

**tmux:**
- Open a game from the stats page and from the launcher card; step, jump,
  auto-play, and esc returns to each.
- `--replay` with a one-game file and with a multi-game file (the picker,
  then esc back to it).
- Play from here → the engine replies, and the game is saved when finished.
- A bad move in a file shows `stopped at move N`.

## Out of scope

- Editing, annotating or exporting from a replay.
- Showing comments.
- Variations.
- Engine analysis (a separate project).
