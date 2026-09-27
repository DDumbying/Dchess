# Onboarding redesign

**Status:** approved design, 2026-09-27
**Branch:** `feat/onboarding` (from `fix/polish`; rebase onto main once PR #9 merges)

## Goal

The first launch and the launcher should look finished: a welcome with the
dchess logo, and a launcher that fills the screen with useful things
instead of empty boxes. The game and stats screens are not changed.

## Shared art: `src/tui/art.c`, `headers/tui/art.h`

```c
void draw_logo(WINDOW *w, int y, int x, int small);          /* small: plain "dchess" */
void draw_mini_board(WINDOW *w, int y, int x, const Position *pos);
int  logo_width(int small);
```

**Logo:** these three rows, verbatim (18 columns), in the theme's accent
colour.

```
╺┳┓┏━╸╻ ╻┏━╸┏━┓┏━┓
 ┃┃┃  ┣━┫┣╸ ┗━┓┗━┓
╺┻┛┗━╸╹ ╹┗━╸┗━┛┗━┛
```

**Mini-board:**
- 8 rows × 16 columns, each square two cells wide, White at the bottom;
- squares use the theme's light and dark colours, and pieces use its piece
  colours and glyphs, exactly as on the game board.

## Welcome: `src/tui/welcome.c`

```c
int tui_welcome(TUIState *s);   /* 1 = profile created, 0 = quit */
```

**When it runs:**
- Only when `profiles.conf` does not load **and** the launcher is about to
  show.
- Otherwise the silent first run stays unchanged, for gameplay flags,
  `--no-menu` and `--stats`.
- `tui_init` only notes that `profiles.conf` did not load; the welcome runs
  after `initscr`, before `tui_launcher`.

**Layout, centred:**
1. the logo;
2. the tagline `chess in your terminal`;
3. a row `♜ ♞ ♝ ♛ ♚ ♝ ♞ ♜` in the piece colours;
4. the fields `Your name` and `Theme ◂ name ▸`;
5. `⏎ continue`;
6. the footer `↑↓ field  ←→ theme  ⏎ go  esc quit`.

Under 60×20, the logo is drawn small and the piece row is left out.

**Name:**
- prefilled with `$USER`, or `player` if unset;
- edited in place, with the same rules as "new profile";
- an invalid name shows the error on the message line and stays on the
  screen.

**Theme:**
- ←→ cycles through the themes;
- each change calls `init_colors`, so the whole welcome recolours;
- it starts at `--theme` when one was given.

**Keys:**
- **⏎** does what `profiles_first_run` does now (create the profile, import
  the old stats, save), plus the chosen theme. Then it returns 1.
- **esc** returns 0: nothing is written and dchess exits, so the welcome
  shows again on the next launch.
- `KEY_RESIZE` redraws the screen.

## Launcher layout: `src/tui/launcher.c`

**Header (3 rows):**
- the logo on the left;
- `profile · theme` right-aligned, in the hint colour.

**Below the header, three columns:**
- **profiles**, 24 columns wide: unchanged.
- **new game**, which takes the remaining width:
  - rows: White, Black, Pos, **Book**, Theme, then ▶ START GAME;
  - the mini-board sits to the right of the rows and shows the Pos row's
    position (the start position, or the custom FEN).
- **profile card**, 28 columns wide, titled with the profile name, built
  from `stats_view_build`:
  - `12W 4L 2D`;
  - a win-rate bar with a percentage;
  - `streak W3`, or nothing when there is no streak;
  - then the last games, as many as fit, each as
    `W|L|D  opponent  age`, with the opponent from `stats_opponent_name`;
  - with no games: `no games yet` and `⏎ to play`.

The old `recent` panel and the floating `win rate` line are removed.

**Book row:**
- ←→ cycles `built-in` → `off` → the profile's custom path (only when it
  has one, shown as the file's base name);
- it writes `book_choice`, exactly as the in-game `book` command does, and
  starting the game saves it to the profile as now.

**Narrow terminals:**

| Condition | Change |
|-----------|--------|
| width < 100 | no profile card |
| width < 72 | no mini-board |
| height < 22 | the header is the small logo on one row |

The minimum size and the resize handling stay as they are.

**Footer:**
```
⏎ play  tab panel  ↑↓ move  ←→ change  n new  r rename  d delete  e engines  s stats  esc quit
```
It is clipped with `mvw_fit`. Every other key works as it does now.

## Testing

**Unit tests:**
- Core code is unchanged apart from reusing existing calls.
- A new test in `tests/test_profiles.c`: a first run with a chosen theme
  saves that theme (`profiles_first_run` gains a `theme` argument; NULL
  keeps the default).

**tmux** (session `dchess-t`, exact targets, scratch HOME/XDG):
- **Fresh home:**
  - the welcome shows;
  - type a name, then ⏎: `profiles.conf` has the name and theme, and the
    old `stats.dat` totals are imported;
  - esc instead: no `profiles.conf` is written.
- **Theme:** ←→ recolours the welcome and the mini-board.
- **Launcher sizes:**
  - 120×35 and 100×30: all three columns;
  - 80×24: no profile card;
  - 60×20: no mini-board, and the logo is small.
- **Book row:** cycles, and the choice reaches the game (`book` shows in
  the panel on the engine's first reply, or a search with `off`).
- **Flag launches:** `dchess --no-menu` in a fresh home creates the profile
  silently, as before.

## Out of scope

- The game and stats screens.
- Animation.
- New themes.
