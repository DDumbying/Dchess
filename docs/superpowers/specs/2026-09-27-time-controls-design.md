# Time controls

**Status:** approved design, 2026-09-27
**Branch:** `feat/clock` (from `main` at the #14 merge)

## Goal

Real chess clocks:
- presets and custom time controls;
- time odds;
- losing on time;
- engines that budget their own time;
- a low-time warning;
- pause that freezes the clocks.

Untimed play (the count-up clocks) stays the default.

## Core

### `src/game/timectl.c`, `headers/game/timectl.h`

```c
typedef struct { int base_ms[2], inc_ms[2]; } TimeControl;   /* base 0 = untimed */
int  tc_parse(const char *s, TimeControl *out);   /* 1 on success */
void tc_format(const TimeControl *tc, char *buf, size_t n);  /* "5+3", "5+0/1+0", "untimed" */
int  tc_timed(const TimeControl *tc);
const char *tc_category(const TimeControl *tc);   /* bullet, blitz, rapid, classical, untimed */
int  tc_budget_ms(long left_ms, int inc_ms);
```

**Text form:**
- `M+S` is minutes (a decimal is allowed, e.g. `0.5`) plus seconds of
  increment.
- `W/B` gives each side its own, e.g. `5+0/1+0`.
- `untimed`, `off` and `0` mean untimed.

**Checks on parse:**
- a base from 1 s to 10 h;
- an increment from 0 to 600 s;
- anything else is rejected.

**Format:** minutes print without trailing zeros (`0.5+0`, `10+5`); when both
sides are the same, one side is printed.

**Category:** from White's estimated game time, `base + 40·inc`:
- under 3 min is bullet;
- under 10 min is blitz;
- under 60 min is rapid;
- otherwise classical.

**Budget:**
- `left/30 + inc·4/5`, capped at `left/3`, at least 50 ms;
- when `left` is under 50 ms, it is `left/2`.

### `GameState` (`game.c`, `game.h`)

**New fields:**
- `TimeControl tc`;
- `long spent_ms[2]`;
- `int moves_by[2]`;
- `int clock_paused`;
- `struct timespec paused_at`.

**Time source:**
- Timing uses `game_now_ms()`, monotonic, and replaceable by
  `game_set_time_source(long (*fn)(void))`, which the tests use.
- `charge_clock` adds the elapsed ms to `spent_ms[mover]`.
- `white_clock`/`black_clock` stay the whole seconds of `spent_ms`, for
  records and stats.
- Each move increments `moves_by[mover]`; undo decrements it and refunds
  the spent time as now.

**Functions:**

```c
void game_set_time_control(GameState *g, const TimeControl *tc);   /* before move 1 */
long game_time_left(const GameState *g, int side);   /* base + inc·moves_by − spent − running */
long game_time_spent(const GameState *g, int side);  /* untimed display */
void game_clock_pause(GameState *g);
void game_clock_resume(GameState *g);
int  game_check_flag(GameState *g);   /* 1 when it just ended the game */
```

- **Running time:** the time of the current turn counts only once the clock
  has started (after move 1) and while not paused.
- **Flag:** when the side to move has no time left and the game is not over,
  the game ends with one of:
  - `"White loses on time — Black wins!"`;
  - or, when the winner has only a king or a king and one minor piece,
    `"Time out, insufficient material — Draw!"`.
- **Record:** `records_end_reason` maps these to `time`.

## Engines

**Built-in:** `opponent_start` in a timed game searches for
`tc_budget_ms(left, inc)`, with the level's depth as the cap.

**UCI:**
- `uci_go_command(const EngineEntry *e, const GameState *g, char *buf, size_t n)`:
  - timed: `go wtime W btime B winc WI binc BI`;
  - untimed: `go depth N` or `go movetime T`, as now.
- The driver uses it.
- A UCI search in a timed game also gets a local deadline of `left + 1000` ms,
  after which the driver sends `stop`.

**The loop:** the game loop calls `game_check_flag` each tick. When it ends
the game while an engine is thinking, the search is cancelled.

## TUI

**Choosing:**
- The launcher gets a **Clock** row after Pos. It cycles `untimed`, `1+0`,
  `3+0`, `3+2`, `5+0`, `5+3`, `10+0`, `10+5`, `15+10`, `30+0`, then
  `custom…`.
- ⏎ on `custom…` prompts `Clock: `. An invalid entry shows
  `Use minutes+seconds, e.g. 5+3 or 5+0/1+0`.
- Saved on the profile as `clock = …`, with `untimed` omitted.
- `--clock TC` sets it for one run (not remembered, like `--book`), and a bad
  value is a CLI error.

**Clocks panel:**
- **Timed:**
  - each side shows time left as `m:ss`, or `h:mm:ss` from an hour;
  - under 10 s it shows `s.t` in `CP_STATUS_ERR` bold;
  - the bars are left/base;
  - the title is `clocks · 5+3`.
- **Untimed:** as now.

**Pause:**
- In a timed game Space and `pause`/`resume` freeze and restart the clocks,
  with or without engines, and the status says `Clocks paused`.
- Engines are held as now.
- `go` and moves are refused while paused in a timed game (`Resume first`).

**Game over:** the flag message uses the existing popup.

## Records and stats

**Tags:**
- `records_append` writes `[TimeControl "300+3"]` (PGN seconds form, White's).
- With odds it also writes `[BlackTimeControl "60+0"]`.
- `Record` gains `char tc[24]`, set in the `tc_format` form: `5+3`, or
  `5+0/1+0` with odds.
- Records with no tag are untimed.

**Stats page:**
- `StatsView` gains `SvTc by_tc[5]`, holding W-D-L per category.
- The endings panel gets a `time` row.
- A new small **time controls** panel sits beside the endings; it lists only
  the categories that have games.

## Testing

**`tests/test_timectl.c`:**
- `tc_parse`/`tc_format` round trips: `5+3`, `0.5+0`, `5+0/1+0`, `untimed`;
- rejects `abc`, `5+`, `-1+0` and `0+5`;
- categories;
- budget bounds.

**`tests/test_game.c`**, with a fake time source:
- time left after moves with an increment;
- undo refunds;
- pause freezes the clock;
- a flag loses on time, and a flag against a bare king is a draw;
- untimed games never flag.

**`tests/test_uci.c`:** `uci_go_command` in timed and untimed games.

**`tests/test_records.c`:** the `TimeControl`/`BlackTimeControl` round trip,
and `EndReason time`.

**`tests/test_cli.c`:** `--clock 5+3` works, and `--clock x` is an error.

**`tests/test_statsview.c`:** `by_tc` counts.

**tmux:**
- `--clock 0.1+0` against Hard: someone flags with the popup, and the game is
  saved with `EndReason time`;
- a red clock under 10 s;
- Space freezes both clocks in a human-vs-human 1+0 game;
- `--clock 3+0/1+0` shows the odds;
- the launcher Clock row, including custom and an invalid entry;
- stats show the time controls panel.

## Out of scope

- Delay or Bronstein clocks.
- Multi-stage controls (40/90 + 30).
- Clock sounds.
- Pre-moves.
