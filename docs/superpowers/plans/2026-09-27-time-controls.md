# Time Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Timed games with presets, odds, flagging, engines that budget their time, a low-time warning, and a pause that freezes the clocks.

**Architecture:**
- `game/timectl.c` handles time-control text and budgets.
- `GameState` keeps millisecond clocks behind a replaceable time source.
- The drivers read the clocks.
- The TUI, CLI, records and stats pick up the time control.

**Tech Stack:** C, ncursesw, `make test`, tmux.

**Spec:** `docs/superpowers/specs/2026-09-27-time-controls-design.md`

> Terse by request. The spec holds the API and values.

## Global Constraints

- Zero warnings under `-O2 -Wall`; use `make -B` after a header change.
- Core never includes ncurses. Sparse comments. No attribution trailers.
- An untimed game must behave exactly as before, including records and the count-up clocks.
- tmux: session `dchess-t` with exact targets, and a scratch HOME/XDG. Typed commands need `i` first.

## Review Focus

1. **Undo in a timed game**, including undoing past the first move and undoing while paused: the time left is refunded consistently, and it never goes above base + increments. Test in Task 2.
2. **Flagging during an engine search, or during the game-over popup:** exactly one result, the engine is cancelled, and no move is applied after the flag. tmux check in Task 3.
3. **Loading a FEN, or starting a new game, mid-timed-game:** the clocks reset to the base, and `moves_by` resets. Test in Task 2.
4. **Time odds where one side is untimed** (`5+0/0`): rejected, since both sides must be timed or neither. Test in Task 1.
5. **Pausing before move 1, and resuming twice:** no negative or doubled time. Test in Task 2.

---

### Task 1: `timectl`

**Files:** Create `headers/game/timectl.h`, `src/game/timectl.c`, `tests/test_timectl.c`.

- [ ] **Step 1: Tests.**
  - `5+3` round-trips.
  - `0.5+0` → base 30000 ms, prints `0.5+0`.
  - `5+0/1+0` → odds, prints the same.
  - `untimed`/`off`/`0` → untimed, prints `untimed`.
  - Rejected: `abc`, `5+`, `-1+0`, `0+5`, `5+0/0`, `601+0` is accepted (601 minutes is under 10 h), `5+601`.
  - Categories: `1+0` bullet, `3+2` blitz (3 + 80 s = 4.3 min), `15+10` rapid, `90+30` classical.
  - `tc_budget_ms`: (300000, 0) → 10000; (300000, 3000) → 12400; (1000, 0) → 50 (33 raised to the 50 ms minimum); (40, 0) → 20; the cap at left/3.
- [ ] **Step 2:** Watch them fail.
- [ ] **Step 3:** Implement per the spec.
- [ ] **Step 4:** Run `make -B test`. Commit `feat(clock): time-control parsing and budgets`.

### Task 2: Millisecond clocks in `GameState`

**Files:** `headers/game/game.h`, `src/game/game.c`, `src/game/records.c` (end reason), `tests/test_game.c`.

- [ ] **Step 1: Tests**, with a fake time source (`static long fake_now; long fake(void){return fake_now;}`):
  - `game_set_time_control(5+2)`.
  - Moves at +3 s and +5 s → White's time left is 300000 − 3000 + 2000.
  - Undo refunds it.
  - A pause at +1 s, then +60 s paused, then resume → time left is unchanged by the paused 60 s.
  - Pausing before move 1 is harmless, and resuming twice changes nothing.
  - A flag: 0.1+0, advance 7 s → `game_check_flag` returns 1 with the result `White loses on time — Black wins!`, or the draw text when the winner is a bare king (FEN `8/8/8/4k3/8/8/4P3/4K3 w`, White flags, Black has a bare king → draw).
  - An untimed game never flags.
  - `game_load_fen`/`game_reset` reset `spent_ms` and `moves_by`.
  - `records_end_reason("White loses on time — Black wins!")` is `time`.
- [ ] **Step 2:** Watch them fail.
- [ ] **Step 3: Implement.**
  - `game_now_ms` is monotonic, overridable.
  - `charge_clock` works in ms, and `white_clock`/`black_clock` are derived.
  - `moves_by` is maintained in `game_play`/`game_undo`.
  - Pause and resume shift the turn start.
  - Flag per the spec, reusing the insufficient-material helper for the winner's side only.
- [ ] **Step 4:** Run `make -B test` (untimed behaviour unchanged). Commit `feat(clock): millisecond clocks, increments, pause and flag`.

### Task 3: Engines, the loop and the panel

**Files:** `src/game/opponent.c`, `src/game/uci.c` + `headers/game/uci.h`, `tests/test_uci.c`, `src/tui/tui.c`, `src/tui/commands.c`, `src/tui/panels.c`.

- [ ] **Step 1: Test.** `uci_go_command`:
  - timed 5+3 with White to move after 2 moves → `go wtime … btime … winc 3000 binc 3000` with the right values;
  - untimed with a depth entry → `go depth 8`;
  - untimed with a time entry → `go movetime 1000`.
- [ ] **Step 2:** Watch it fail; implement it, and have `send_search` use it. Add the UCI local deadline of left + 1000 ms, which sends `stop`.
- [ ] **Step 3:** In a timed game, the built-in `builtin_start` time is `tc_budget_ms(game_time_left, inc)`.
- [ ] **Step 4: Loop.**
  - Each tick runs `if (game_check_flag(&state->game)) cancel_engine_search(state);`.
  - Pause and resume call `game_clock_pause`/`resume` when timed, and are allowed in timed games without engines.
  - Moves and `go` while paused in a timed game → `Resume first`.
- [ ] **Step 5: Panel.** A timed clocks panel per the spec, with the title `clocks · 5+3`.
- [ ] **Step 6:** Run `make -B dchess && make -B test`. tmux, with `--clock` added temporarily through `state->game.tc` via Task 4's CLI (do Task 4's CLI step first if simpler, and ledger the ruling):
  - 0.1+0 against Hard: a flag with the popup;
  - red under 10 s;
  - Space freezes the clocks between two humans.
- [ ] **Step 7:** Commit `feat(clock): engines budget the clock, flags end the game, timed panel`.

### Task 4: Choosing a time control, records, stats

**Files:** `headers/utils/cli.h`, `src/utils/cli.c`, `tests/test_cli.c`, `headers/game/profiles.h`, `src/game/profiles.c`, `tests/test_profiles.c`, `src/tui/tui.c`, `src/tui/launcher.c`, `headers/game/records.h`, `src/game/records.c`, `tests/test_records.c`, `headers/game/statsview.h`, `src/game/statsview.c`, `tests/test_statsview.c`, `src/tui/stats_tui.c`, `README.md`.

- [ ] **Step 1: Tests.**
  - CLI: `--clock 5+3` → `args.clock`, and `--clock x` is an error.
  - Profiles: `clock = 5+0/1+0` round-trips.
  - Records: an appended timed game writes `TimeControl "300+3"` and `BlackTimeControl` with odds; loading sets `r.tc`; `EndReason time`.
  - Statsview: `by_tc` counts by category.
- [ ] **Step 2:** Watch them fail; implement.
  - `tui_init` applies `args.clock`, or else the profile's `clock`.
  - The launcher Clock row cycles the presets, with `custom…` prompting.
  - `tui_remember_setup` saves the clock unless it came from `--clock`.
  - The stats page gets a time-controls panel and the `time` ending.
- [ ] **Step 3:** Run `make -B dchess && make -B test`.
- [ ] **Step 4: tmux:**
  - the launcher row, including custom and an invalid entry;
  - `--clock 3+0/1+0` shows the odds;
  - a flagged game is saved with `EndReason time`, and the stats page shows the time-controls panel.
- [ ] **Step 5:** Update the README. Commit `feat(clock): choose, remember and record time controls`.
