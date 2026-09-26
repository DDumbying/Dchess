# Stats Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A dashboard stats page (layout A) built on `games.pgn`.

**Architecture:** The core module `game/statsview.c` builds a `StatsView` from records and a profile, and is unit-tested. `tui/stats_tui.c` is rewritten to draw it. The old `DchessStats` screens and adapters are removed.

**Tech Stack:** C, ncursesw, `make test`.

**Spec:** `docs/superpowers/specs/2026-09-26-stats-page-design.md`

> Terse by request. The spec holds the struct and every value.

## Global Constraints

- Zero warnings under `-O2 -Wall`. Use `make -B` after a header change.
- Core never includes ncurses. Sparse comments. No attribution trailers.
- tmux: use a unique session and exact targets only (`-s dchess-t`, `-t '=dchess-t:'`), with a scratch HOME and XDG.

## Review Focus

1. **A profile with 0 games:** the panels show "no games yet" or zeroes, with no division by zero. Test: Task 1, empty history.
2. **More than 64 opponents:** capped at `SV_OPP_MAX`, with no overflow. Test: Task 1, 70 guests… engines named `E0`..`E69`.
3. **A resize while the page is open** redraws, and below 80×24 uses the stacked layout. Checked with tmux in Task 2.
4. **An opponent name wider than its column** (UTF-8 included) is clipped inside the panel. Checked with tmux in Task 2, using a long Cyrillic engine name.
5. **←/→ with a single profile** does nothing. Checked with tmux in Task 2.

---

### Task 1: `statsview.c`

**Files:** `headers/game/statsview.h`, `src/game/statsview.c`, `tests/test_statsview.c`

- [ ] **Step 1: Header.** Use the struct and function exactly as the spec's Data section gives them. Include `game/records.h` and `game/profiles.h`.
- [ ] **Step 2: Test.** `test_statsview.c` writes a fixture `games.pgn` in a temp dir with `fprintf` (tags only; movetext `1-0` etc). Set `now` = 1800000000 and put `Date`/`Time` on each game relative to it. Profile `saeed` has legacy {games 4, wins 2, losses 1, draws 1} and plays:
  - vs `dchess (Hard)` (`BlackKind dchess`, `BlackStrength Hard`): W, then L, both as White, 1 day ago;
  - vs `alice` (profile): W as Black, 3 days ago; D as White, 10 days ago;
  - vs `Guest`: a mate L, 20 days ago;
  - vs engine `Fake`: W resign, now;
  - one legacy stub W, 40 days ago.

  Assert:
  - `total` = legacy + recorded;
  - the `as_white` / `as_black` counts;
  - `streak` = `W` 1; `best_win_streak`;
  - `per_week`: [0] = 3 (Fake, and dchess ×2 at 1 day), [1] = 1, [2] = 1, [3] = 1;
  - opponent rows are named `dchess Hard`, `alice`, `Guest`, `Fake` and `before profiles`, sorted by games, and dchess Hard is 1-0-1;
  - `mates`, `resigns`, `avg_plies`;
  - `recent[0]` is the Fake game;
  - `trend_count` counts the legacy stub too.

  Also:
  - **empty history:** all zero, no crash;
  - **70 distinct engines:** `opp_count` = `SV_OPP_MAX`.

  Order the file so the streak logic is exercised: the last games are L, then W.
- [ ] **Step 3:** `make build/test_statsview`. Expected: `statsview.h` missing.
- [ ] **Step 4: Implement.**
  - The opponent name comes from the other side's kind: dchess → `dchess ` + strength (or `dchess` if blank); guest → `Guest`; otherwise the tag name.
  - Accumulate rows with a linear search; add the legacy row when `legacy_games > 0`; sort with `qsort` by games, descending.
  - Trend: `records_winrate(l, name, trend, SV_TREND)` for the series. Compute the streaks in the same pass, over all of the profile's games in file order.
  - Recent: `records_recent(l, name, recent, 256)`.
- [ ] **Step 5:** Tests pass, then run `make -B test`. Commit `feat: stats view built from the game database`.

---

### Task 2: Stats screen

**Files:** rewrite `src/tui/stats_tui.c` and `headers/tui/stats_tui.h`; update `src/tui/launcher.c`, `src/tui/commands.c`, `src/tui/tui.c` and `src/main.c`.

**Produces:**
- `void stats_screen(TUIState *s)` (modal);
- `void stats_mini(WINDOW *parent, const TUIState *s)` (the Tab popup);
- `void stats_standalone(void)` for `--stats`, which builds its own profile list and records.

- [ ] **Step 1: Draw layout A** with `panel_frame` and the theme's pairs.
  - The sparkline uses `CP_RAMP_BASE + dash_ramp_index`, as the launcher does.
  - Ending bars use `dash_bar_fill`.
  - Opponent win % is green at ≥55, yellow at ≥45, red below.
  - Below 80×24, stack summary, opponents and recent.
  - Clip every text field to its panel width.
- [ ] **Step 2: Keys.** ←→ switch profile and rebuild; Tab focuses opponents or recent; ↑↓ scroll; Esc returns; `KEY_RESIZE` redraws.
- [ ] **Step 3: Wire it in.**
  - launcher `s` → `stats_screen`;
  - in-game `stats` → `stats_screen`, then force a repaint of the game;
  - Tab → `stats_mini`;
  - `main --stats` → `stats_standalone`.
- [ ] **Step 4:** Build clean, then run `make -B test`.
- [ ] **Step 5: tmux** at 120×40 and 70×20, with the scratch HOME from Task 1's style of fixture (profiles `saeed`, `alice` plus records, and an engine with a 30-character Cyrillic name):
  - every panel draws;
  - ←→ switches profile;
  - Tab and ↑↓ scroll;
  - resize from 120×40 to 70×20 → stacked;
  - with one profile, ←→ is a no-op;
  - the page opens from the launcher `s`, from in-game `stats`, and from `--stats`.
- [ ] **Step 6:** Commit `feat(tui): dashboard stats page`.

---

### Task 3: Remove the old stats path

**Files:** `src/game/records.c` and `headers/game/records.h` (`records_to_stats`); `src/game/profiles.c` and `headers/game/profiles.h` (`profiles_stats`); `headers/tui/tui.h` (`stats`); `src/tui/commands.c` (`tui_refresh_stats`); tests `test_records.c` and `test_profiles.c`.

- [ ] **Step 1:** Delete the functions and their tests (`test_to_stats`, `test_stats`). Delete `TUIState.stats` and `tui_refresh_stats`; its callers now use the stats screen. Keep `stats_load` for first run: `tui_init` loads into a local `DchessStats` for `profiles_first_run`.
- [ ] **Step 2:** Run `grep -rn "records_to_stats\|profiles_stats\|tui_refresh_stats\|state->stats\|draw_stats_overlay\|show_stats_overlay\|draw_stats_mini" src headers tests`. Expected: no matches.
- [ ] **Step 3:** Build clean, then run `make -B test`. Commit `refactor: retire the old stats adapters`.
