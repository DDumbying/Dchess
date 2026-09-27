# Onboarding Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A first-run welcome and a dashboard launcher with a logo, a mini-board and a profile card.

**Architecture:**
- `tui/art.c` draws the logo and a mini-board, and both screens use it.
- `tui/welcome.c` runs once, before the launcher, when `profiles.conf` is missing.
- `launcher.c` gets a new layout and keeps its key handling.
- The only core change is a `theme` argument for `profiles_first_run`.

**Tech Stack:** C, ncursesw, `make test`, tmux.

**Spec:** `docs/superpowers/specs/2026-09-27-onboarding-design.md`

> Terse by request. The spec holds the layout values.

## Global Constraints

- Zero warnings under `-O2 -Wall`; use `make -B` after a header change.
- Core never includes ncurses. Sparse comments. No attribution trailers.
- Clip every string with `mvw_fit`/`mvw_clip`; never use a raw `mvwprintw` of a name.
- tmux: session `dchess-t`, targets `'=dchess-t:'` only, with a scratch HOME/XDG_CONFIG_HOME/XDG_DATA_HOME.

## Review Focus

1. **A bad name in the welcome:** an empty name, `root;x`, 97+ bytes or CJK. It must show an error and keep the screen; CJK must be accepted. tmux check in Task 3.
2. **Resizing during the welcome:** to below 60×20, then to 30×10. It must switch to the small logo or show a "too small" message, and never crash. tmux check in Task 3.
3. **esc after changing the theme:** no file is written, and the terminal is restored. tmux check in Task 3.
4. **Book row with a custom path:** cycling off and back must restore the same path, and a long path shows only its base name. tmux check in Task 4.
5. **Profile card edge cases:** a profile with only pre-profile ("legacy") games shows those totals; long or CJK opponent names clip cleanly. tmux check in Task 4.

---

### Task 1: The first run keeps a chosen theme

**Files:** `headers/game/profiles.h`, `src/game/profiles.c`, `tests/test_profiles.c`, callers `src/tui/tui.c`, `src/tui/stats_tui.c`

- [ ] **Step 1: Test.** Add to `test_first_run` (from a fresh path):
  ```c
  check("a chosen theme is saved",
        profiles_first_run(&l, "saeed", NULL, games, "nord") == 1 &&
        strcmp(l.p[0].theme, "nord") == 0);
  ```
  Update the existing calls to pass `NULL`.
- [ ] **Step 2:** `make build/test_profiles` → too many arguments.
- [ ] **Step 3: Implement.**
  - Signature: `profiles_first_run(ProfileList*, const char *user, const DchessStats*, const char *games, const char *theme)`.
  - When `theme` is set, copy it into `p->theme` before `profiles_save`.
  - The callers pass `NULL`.
- [ ] **Step 4:** Run `make -B test` → all pass, 0 warnings. Commit `feat(profiles): first run can take a theme`.

### Task 2: Logo and mini-board

**Files:** Create `headers/tui/art.h`, `src/tui/art.c`. Modify `headers/tui/render.h` and `src/tui/render.c`.

- [ ] **Step 1: render.c.** Export the glyph writer:
  ```c
  void render_piece(WINDOW *win, int r, int c, int piece, attr_t attr);  /* wraps put_glyph */
  ```
- [ ] **Step 2: art.c.**
  - `logo_width(small)` returns 18, or 6 when small.
  - `draw_logo` prints the spec's three rows in `CP_TITLE | A_BOLD`. When small, it prints `dchess` on one row.
  - `draw_mini_board`:
    - 8 rows, rank 8 at the top;
    - each square 2 columns, `COLOR_PAIR(light ? CP_LIGHT : CP_DARK)`, filled with spaces;
    - a piece is drawn with `render_piece` and `CP_W_LIGHT/CP_W_DARK/CP_B_LIGHT/CP_B_DARK | A_BOLD`, as `draw_board_grid` does;
    - `light = (rank + file) % 2 == 1`, matching the game board;
    - the piece on a square comes from a loop over `pos->bitboards[0..11]`.
- [ ] **Step 3:** Run `make -B dchess` → 0 warnings. Commit `feat(tui): logo and mini-board art`.

### Task 3: Welcome screen

**Files:** Create `headers/tui/welcome.h`, `src/tui/welcome.c`. Modify `src/tui/tui.c`, `headers/tui/tui.h`, `Makefile` (only if `SRC` is not a wildcard).

- [ ] **Step 1: tui.h/tui.c.**
  - Add `int first_run` to `TUIState`.
  - In `tui_init`, when `profiles_load` fails:
    - if `show_onboarding` is set, set `first_run = 1` and do not call `profiles_first_run`;
    - otherwise run the silent first run as now.
  - In the curses start, before `tui_launcher`:
    ```c
    if (state->first_run && !tui_welcome(state)) { endwin(); /* same exit as a launcher quit */ }
    ```
    Match the existing launcher-quit exit path.
- [ ] **Step 2: welcome.c.**
  - Layout, centred as in the spec: the logo; the tagline in `CP_HINT`; the piece row using `render_piece` for pieces 6..11 on the canvas pair; the name field; the theme field; `⏎ continue`; and the footer.
  - The focused field gets `▸` and the name shows a cursor bar `▏`.
  - **Keys:**
    - printable UTF-8 goes into the name (a `wget_wch` loop; keep the name to `PLAYER_NAME_MAX` bytes);
    - Backspace removes one UTF-8 character;
    - ↑↓ switch fields;
    - ←→ on the theme field changes `s->theme`, then `init_colors`;
    - ⏎ checks the name with `profiles_add` on a scratch `ProfileList` (for its error text). If the name is valid: `stats_load(&old)`, `records_path`, then `profiles_first_run(&s->profiles, name, &old, games, theme_name(s->theme))`, set `s->file_active`, and return 1. If not, put the error on the message line.
    - esc returns 0.
  - Under 60×20: the small logo and no piece row. Under 40×12: `terminal too small` centred.
  - `KEY_RESIZE` redraws.
- [ ] **Step 3:** Run `make -B dchess && make -B test` → 0 warnings, all pass.
- [ ] **Step 4: tmux (100×30, fresh home with an old `stats.dat` copied in):**
  - the welcome shows;
  - ←→ changes the colours;
  - typing a CJK name then ⏎ reaches the launcher, and `profiles.conf` has the name and `theme =`;
  - the old totals show in the card or on the stats page.
  - Review Focus 1–3:
    - `root;x` → an error, the screen stays;
    - resize to 50×18 and then 30×10 → small logo, then "too small";
    - esc → no `profiles.conf`.
  - `dchess --no-menu` in a fresh home → silent first run.
- [ ] **Step 5:** Commit `feat(tui): first-run welcome`.

### Task 4: Dashboard launcher

**Files:** `src/tui/launcher.c`, `README.md`

- [ ] **Step 1: Layout.** Replace `draw()`, `draw_recent()` and `draw_winrate()`.
  - Header: `draw_logo` at (0,1), plus `profile · theme` right-aligned in `CP_HINT`. Use the small, one-row form when `LINES < 22`.
  - Columns under the header:
    - profiles: `LEFT_W = 24`;
    - card: `CARD_W = 28`, only when `COLS >= 100`;
    - new game: the rest.
  - The mini-board goes at the right of new game when `COLS >= 72`. Its `Position` is parsed from the Pos row: the start FEN, or `L->custom_fen`, using the launcher's existing FEN loader.
  - Card:
    - `stats_view_build(&L->rec, profile, time(NULL), &sv)`, into a static `StatsView` because it is large;
    - the rows, in order: `%dW %dL %dD` of `sv.total`; a win-rate bar (the width of the card minus 7, then ` %3d%%`); `streak %c%d` when `sv.streak`;
    - a blank row, then `sv.recent` for as many as fit, as `W|L|D`, the name from `stats_opponent_name`, then the age;
    - with no games: `no games yet`, `⏎ to play` in `CP_HINT`.
- [ ] **Step 2: Book row.**
  - Add `ROW_BOOK` between `ROW_POSITION` and `ROW_THEME`.
  - Its value is `built-in`/`off`/the `basename` of the path.
  - ←→ cycles through `builtin`, `off`, and the profile's `book` when it is a path. It writes `s->book_choice`, as the `book` command's setter does (reuse it if it is callable; otherwise set `book_choice` and let `start()` attach it).
- [ ] **Step 3: Footer.** Put the spec's footer string through `mvw_fit`. Update the README's launcher section in one or two lines.
- [ ] **Step 4:** Run `make -B dchess && make -B test` → 0 warnings, all pass.
- [ ] **Step 5: tmux:**
  - at 120×35 and 100×30: three columns;
  - at 80×24: no card;
  - at 60×20: no mini-board, small logo;
  - a theme change recolours the mini-board;
  - a custom FEN shows on the mini-board.
  - Review Focus 4: with `book = /tmp/x/very/long/path/book.bin` in the profile, cycling shows `book.bin` and comes back to it.
  - Review Focus 5: a profile with only a legacy line shows its totals; a CJK opponent clips cleanly.
  - Book `off` → the engine's first reply is a search, not `book`.
- [ ] **Step 6:** Commit `feat(tui): dashboard launcher`.
