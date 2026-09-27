# Analysis mode

**Status:** approved design, 2026-09-27
**Branch:** `feat/analysis` (from `main` at `037698a`)

## Goal

Show what an engine thinks of the position, while playing or replaying:
- an eval bar and score;
- the best move, with its squares tinted on the board;
- the expected line.

In a replay, a full game review marks mistakes.

## Core

### `src/engine/search.c`: `int search_pv(const Position *pos, Move *out, int max)`

- Follows the transposition table's `best` moves from `pos`, up to `max`.
- Stops at a missing entry, a move that is not legal, or a position already
  on the line.
- Returns the count.
- Call it after a search, from the same thread, when no other search is
  running.

### `src/game/analysis.c`, `headers/game/analysis.h`

```c
typedef struct {
    int  score_cp;            /* White's view */
    int  mate;                /* 0, or mate in N (+ White mates, - Black mates) */
    int  depth;
    Move best;                /* 0 if none */
    char line[8][8]; int line_len;   /* SAN, starting with best */
} Analysis;

typedef struct Analyser Analyser;
Analyser *analyser_builtin(int time_ms);
Analyser *analyser_uci(const EngineEntry *e, int time_ms);
void analyser_free(Analyser *a);
int  analyser_start(Analyser *a, const GameState *g);   /* cancels any running one */
int  analyser_poll(Analyser *a, Analysis *out, U64 *key); /* 1 when a result for `key` is ready */
void analyser_stop(Analyser *a);
int  analyser_is_builtin(const Analyser *a);

/* How much a move lost for the side that played it, graded. */
typedef enum { GRADE_NONE, GRADE_INACCURACY, GRADE_MISTAKE, GRADE_BLUNDER } Grade;
Grade review_grade(const Analysis *before, const Analysis *after, int mover);
const char *grade_mark(Grade g);   /* "", "?!", "?", "??" */
```

**Built-in analyser:**
- Uses `opponent_builtin(MAX_DEPTH, time_ms, NULL, DIFF_HARD)` (no book).
- On a result, it builds `line` from `search_pv` played forward with
  `san_write`.

**UCI analyser:**
- Uses the UCI driver with `go movetime time_ms`.
- It keeps the last `info` line that has a `score`, and its `pv`.
- `uci_info_parse` gains `pv` (up to 8 moves); the moves are converted to SAN
  on a copy of the game.

**Scores:**
- Engine scores are from the side to move, so they are flipped to White's
  view.
- Mate `n` becomes `mate = ±n`, with `score_cp = ±(30000 - |n|)` for grading.

**Grading:**
- The loss is `before` minus `after` from the mover's view, in centipawns,
  with each score clamped to ±1500.
- `≥300` is `??`, `≥100` is `?`, `≥50` is `?!`.
- A move that keeps a forced mate for the mover is never graded.

## TUI

**State** (`TUIState`):
- `Analyser *analyser`;
- `int analysis_on`;
- `Analysis analysis`;
- `U64 analysis_key`;
- `char analysis_engine[ENGINE_NAME_MAX + 1]` (empty = built-in).

**Choosing the engine:**
- The launcher gets an **Analysis** row after Book. It cycles `off` →
  `built-in` → each registered engine.
- Saved on the profile as `analysis = off|builtin|<engine>`; the default is
  `off`.
- The in-game `analyse <builtin|off|name>` command sets it for this run and
  remembers it, as `book` does.

**Keys and running:**
- `a` in normal mode (game screen) and in replay toggles `analysis_on`, using
  the chosen engine, or built-in when that is `off`.
- It restarts when the position key changes.
- A built-in analyser is stopped and not started while any built-in search
  (opponent or `go`) runs; the panel then says `engine thinking`.
- A UCI analyser runs any time.

**Analysis panel** (replaces the eval panel while `analysis_on`):
- the title `analysis · <engine>`;
- a vertical bar: White's share from the clamped score, on a logistic scale
  (`1/(1+10^(-cp/400))`);
- `+1.25 d14` or `M3 d20`;
- `best: Nf3`;
- the line, wrapped to the panel;
- `analysing…` until the first result.

**Board:** the best move's from and to squares get a tint (new colour pairs
`CP_HINT_SQ_L/D`) while `analysis_on` and not in a game-over position.

## Game review (replay)

- **`r`** starts a review:
  - every position `0..count` is analysed in turn at 300 ms (built-in) or
    `movetime 300` (UCI), in the background of the replay loop;
  - the command bar shows `reviewing k/n`;
  - stepping still works;
  - `r` again cancels.
- **Results:**
  - `ReplayReview { Analysis at[MAX_MOVE_HISTORY + 1]; int done; }` is held
    by the replay (freed with it);
  - each move gets `grade_mark` from `review_grade(at[i], at[i+1], mover)`;
  - the moves panel shows the marks after the SAN.
- **Keys:** `n` / `N` jump to the next or previous graded move; with none, the
  status says `no mistakes found`.
- The analysis panel shows `this move: ?? (−3.40)` for the current move once
  it is reviewed.
- Nothing is saved.

## Testing

**`tests/test_analysis.c`:**
- `review_grade` thresholds for both movers;
- clamping;
- mates: keeping a mate is not graded, and allowing one is `??`.
- `search_pv` after a search from a tactical position returns legal moves,
  and the first one equals `best_move`.
- The built-in analyser at the start position returns a legal best move and
  a line whose first move matches it.
- The UCI analyser with `fake_uci` mode `analyse` (new: sends
  `info depth 5 score cp 35 pv e2e4 e7e5 g1f3`) gives `+0.35` (White to
  move), the line `e4 e5 Nf3`, and the same score flipped when Black is to
  move.
- The score flip for mate scores.

**`tests/test_uci.c`:** `uci_info_parse` reads `pv`.

**`tests/test_profiles.c`:** `analysis =` round-trips.

**tmux:**
- `a` on and off in a game and in a replay; the bar, best move and line
  appear;
- the built-in opponent thinking shows `engine thinking`, then analysis
  resumes;
- the launcher Analysis row and the `analyse` command;
- `r` review in a replay with a known blunder (`1. f3 e5 2. g4?? Qh4#`) marks
  `g4??`, and `n` jumps there.

## Out of scope

- Multi-PV.
- Infinite analysis.
- Arrows.
- Saving reviews or annotations to PGN.
- Analysis in the stats page.
