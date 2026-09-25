# Dashboard UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the in-game TUI as a btop-style dashboard (board on the left; eval, clocks, moves and engine panels on the right) with four 256-colour themes, Gruvbox by default.

**Architecture:** Themes become tables of xterm-256 palette indices, applied through the existing colour-pair registry with no `init_color()`. Pure layout and graph maths go into a new ncurses-free module `utils/dash` so they can be unit-tested; drawing goes into a new `tui/panels.c`, with `render.c` reduced to the board and orchestration. Two data bugs the panels depend on are fixed first.

**Tech Stack:** C (gcc, `-O2 -Wall`), ncursesw, pthreads, the repo's own `check()`-style test harness run by `make test`.

**Spec:** `docs/superpowers/specs/2026-09-25-dashboard-ui-design.md`

## Global Constraints

- Build must stay at **zero warnings** (`make 2>&1 | grep -cE 'error|warning'` prints `0`).
- `make test` must pass after every task. Tests link only `src/engine`, `src/game`, `src/utils` (`CORE_SRC`); nothing under `src/tui` is testable, so logic that needs a test goes in `src/utils` or `src/game`.
- Themes are xterm-256 palette indices. No `init_color()` anywhere.
- Every colour-pair ID must stay **below 64**: 8-colour terminals commonly offer only 64 pairs.
- Minimum terminal stays **34x20**.
- Side column: hidden below 60 columns, 26 wide from 60, 34 wide from 110.
- Eval graph clamps to **±500 centipawns**.
- Default theme is `gruvbox`; the others are `tokyonight`, `btop`, `catppuccin`. Old names are rejected.
- Commit messages carry no AI attribution lines. Comments stay sparse (the repo runs at roughly 5-10% comment lines).

## Review Focus

1. **8-colour terminal with only 64 colour pairs** (e.g. `TERM=xterm`): the app must start and stay legible, with no pair ID ≥ 64. Pinned by a `_Static_assert` and a test in Task 3.
2. **Terminal 60-110 columns wide, or narrower than 60**: the side column must take the right width or disappear, never overlap the board. Pinned by `dash_side_width` boundary tests in Task 4.
3. **Short terminal (20-25 rows)**: side panels must shrink and drop in order rather than draw past the window. Pinned by `dash_side_layout` tests in Task 4.
4. **Mate scores in the evaluation history** (values near ±999000): the graph must clamp rather than overflow the panel. Pinned by `dash_graph_fill` tests in Task 4.
5. **A game longer than the graph is wide**: the graph must show the most recent evaluations. Pinned by `dash_graph_start` tests in Task 4.

---

## File Structure

| File | Responsibility |
|---|---|
| `headers/game/game.h`, `src/game/game.c` | + `eval_white_view()` |
| `headers/engine/search.h`, `src/engine/search.c` | `SearchResult` gains `elapsed_ms` |
| `headers/utils/theme.h`, `src/utils/theme.c` | Theme tables as 256-palette indices; `theme_contrast()` |
| `headers/utils/dash.h`, `src/utils/dash.c` | **New.** Pure layout and graph maths |
| `headers/tui/colors.h` | Pair registry: + accents, track, ramp, black-piece highlight pairs; − `COL_*`, `SCP_*`, `SCOL_*` |
| `headers/tui/panels.h`, `src/tui/panels.c` | **New.** `panel_frame()`, side column, command bar |
| `src/tui/render.c`, `headers/tui/render.h` | Colour init; board inside its panel; `render_all()` orchestration |
| `src/tui/tui.c`, `headers/tui/tui.h` | Screen = board + side + command windows; `last_search` in `TUIState` |
| `src/tui/commands.c`, `src/tui/input.c` | Eval sign, engine stats, theme list, input row colours |
| `src/tui/onboard.c`, `src/tui/stats_tui.c` | Restyled onto `panel_frame()` and theme pairs |
| `tests/test_theme.c`, `tests/test_dash.c` | **New** suites |

---

### Task 1: Show the evaluation from White's side

`search()` scores from the side to move's point of view (+925 with White to move and a queen up). `apply_engine_result()` flips it the wrong way in both cases, so that position displays as −9.25. The `eval` command never normalises at all, and `eval_history` stores the raw score. This fixes all three.

**Files:**
- Modify: `headers/game/game.h`, `src/game/game.c`
- Modify: `src/tui/commands.c` (`apply_engine_result`, the `eval` command)
- Modify: `docs/superpowers/specs/2026-09-25-dashboard-ui-design.md`
- Test: `tests/test_game.c`

**Interfaces:**
- Produces: `int eval_white_view(int score_cp, int side_to_move);`, which returns `score_cp` when `side_to_move == WHITE` and `-score_cp` otherwise. After this task `eval_history[]` holds White-view centipawns.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_game.c`, near the other helpers:

```c
#include "engine/search.h"

static void test_eval_perspective(void)
{
    printf("== evaluation perspective ==\n");

    check("a White-to-move score is already White's view",
          eval_white_view(925, WHITE) == 925);
    check("a Black-to-move score is negated",
          eval_white_view(-895, BLACK) == 895);

    /* White is a queen up; whoever is to move, White's view is positive. */
    GameState g;
    game_reset(&g);
    game_load_fen(&g, "4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    SearchResult w = search(&g.pos, 3, 0);
    check("queen up, White to move: positive for White",
          eval_white_view(w.best_score, g.pos.side) > 500);

    game_load_fen(&g, "4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
    SearchResult b = search(&g.pos, 3, 0);
    check("queen up, Black to move: still positive for White",
          eval_white_view(b.best_score, g.pos.side) > 500);
}
```

Call it from `main()`, after `test_undo_repeated();`:

```c
    test_eval_perspective();
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test 2>&1 | grep -E 'error|FAIL' | head`
Expected: a build error, `implicit declaration of function 'eval_white_view'`.

- [ ] **Step 3: Implement**

In `headers/game/game.h`, after `U64 game_hash(const GameState *g);`:

```c
/* Search scores are from the side to move's point of view. */
int eval_white_view(int score_cp, int side_to_move);
```

In `src/game/game.c`, at the end:

```c
int eval_white_view(int score_cp, int side_to_move)
{
    return side_to_move == WHITE ? score_cp : -score_cp;
}
```

In `src/tui/commands.c`, `apply_engine_result()`, replace:

```c
    /* Negamax reports for the side that just moved; flip so last_eval is
     * always from White's point of view. */
    int score_white = (state->game.pos.side == BLACK) ? res.best_score : -res.best_score;
    float eval_f = score_white / 100.0f;
    snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", eval_f);
    game_record_eval(&state->game, res.best_score);
```

with:

```c
    int score_white = eval_white_view(res.best_score, state->game.pos.side);
    float eval_f = score_white / 100.0f;
    snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", eval_f);
    game_record_eval(&state->game, score_white);
```

In the `eval` command, replace:

```c
        SearchResult res = search(&state->game.pos, 1, 0);
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", res.best_score/100.0f);
```

with:

```c
        SearchResult res = search(&state->game.pos, 1, 0);
        int score_white = eval_white_view(res.best_score, state->game.pos.side);
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", score_white / 100.0f);
```

In the spec, "Changes this depends on", item 1, replace the sentence beginning "`eval_history` stores the raw search score" with:

```markdown
1. **Eval perspective.** `search()` scores from the side to move's point of
   view, and every consumer got the conversion wrong: `apply_engine_result()`
   negated it in both cases (a queen up for White displayed as −9.25), the
   `eval` command never converted it, and `eval_history` stored it raw. All
   three now go through `eval_white_view()`.
```

- [ ] **Step 4: Run to verify it passes**

Run: `make test 2>&1 | grep -E '^All |FAIL'`
Expected: every suite prints `All ... passed`, no `FAIL`.

- [ ] **Step 5: Commit**

```bash
git add headers/game/game.h src/game/game.c src/tui/commands.c tests/test_game.c docs/superpowers/specs/2026-09-25-dashboard-ui-design.md
git commit -m "fix: show the evaluation from White's side

search() scores from the side to move's point of view. The display
negated it in both cases, so a queen up for White read as -9.25; the
eval command never converted it; and eval_history stored it raw. All
three now go through eval_white_view()."
```

---

### Task 2: Report search time with the result

The engine panel shows nodes per second, which needs the time a search took. The spec suggested timing it in the worker thread; timing it inside `search()` gives every caller the figure, including `make bench`, for the same cost.

**Files:**
- Modify: `headers/engine/search.h`, `src/engine/search.c`
- Test: `tests/test_game.c`

**Interfaces:**
- Produces: `SearchResult.elapsed_ms` (`long`), the wall-clock time the search took.

- [ ] **Step 1: Write the failing test**

Add to `test_eval_perspective()` in `tests/test_game.c`, before its closing brace:

```c
    check("a search reports how long it took", w.elapsed_ms >= 0 && w.nodes > 0);
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test 2>&1 | grep -E 'error' | head -3`
Expected: `'SearchResult' has no member named 'elapsed_ms'`.

- [ ] **Step 3: Implement**

In `headers/engine/search.h`, in the `SearchResult` struct, after `int  depth_reached;`:

```c
    long elapsed_ms;
```

In `src/engine/search.c`, at the top of `search()` right after `SearchResult best = {0, -INF, 0, 0};`:

```c
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
```

and replace the final `best.nodes = node_count;` with:

```c
    best.nodes = node_count;

    struct timespec finished;
    clock_gettime(CLOCK_MONOTONIC, &finished);
    best.elapsed_ms = (finished.tv_sec - started.tv_sec) * 1000L
                    + (finished.tv_nsec - started.tv_nsec) / 1000000L;
```

- [ ] **Step 4: Run to verify it passes**

Run: `make test 2>&1 | grep -E '^All |FAIL'`
Expected: all suites pass.

- [ ] **Step 5: Commit**

```bash
git add headers/engine/search.h src/engine/search.c tests/test_game.c
git commit -m "feat(search): report how long a search took"
```

---

### Task 3: Themes as 256-colour palettes

Replaces the four RGB themes with Gruvbox, Tokyo Night, btop and Catppuccin as xterm-256 indices. Colour init no longer depends on `can_change_color()`, which tmux and screen report false; in those terminals the old themes only ever rendered in the 8-colour fallback. The old layout is kept for now: this task changes colour only.

It also fixes piece colour on highlighted squares. Check, selection, cursor and legal-move squares drew every piece with one pair, and since the block-art pieces share a silhouette, a piece under the cursor could not be told apart as White or Black. Those four roles get separate white and black pairs.

Every index below was chosen by measurement: all 37 contrast checks per theme in Step 1 pass.

**Files:**
- Modify: `headers/utils/theme.h`, `src/utils/theme.c`
- Modify: `headers/tui/colors.h`
- Modify: `src/tui/render.c` (`init_colors`, piece pairs in `draw_board_grid`)
- Modify: `src/tui/stats_tui.c` (drop its private palette)
- Modify: `src/tui/tui.c` (`screen_build` window backgrounds)
- Modify: `src/tui/commands.c`, `src/utils/cli.c` (theme names in messages)
- Modify: `Makefile` (`-lm` for `pow`)
- Test: `tests/test_theme.c` (new)

**Interfaces:**
- Produces, in `theme.h`: `THEME_RAMP` (8); the `Theme` struct below; `double theme_contrast(int a, int b);`
- Produces, in `colors.h`: `CP_ACC_BOARD`, `CP_ACC_EVAL`, `CP_ACC_CLOCK`, `CP_ACC_MOVES`, `CP_ACC_ENGINE`, `CP_TRACK`, `CP_RAMP_BASE` (`CP_RAMP_BASE + i` for `i < THEME_RAMP`), `CP_CURSOR_PC_B`, `CP_SEL_PC_B`, `CP_MOVE_HI_PC_B`, `CP_CHECK_PC_B`, `CP_LAST`. `CP_CANVAS` now means text on the theme background.

- [ ] **Step 1: Write the failing test**

Create `tests/test_theme.c`:

```c
/* Theme tests: every colour combination the board and panels draw must
 * be legible, measured as WCAG contrast.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "utils/theme.h"
#include "utils/cli.h"
#include "tui/colors.h"

static int failures = 0;
static int theme_failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void need(const Theme *t, const char *what, int a, int b, double min)
{
    double c = theme_contrast(a, b);
    if (c < min) {
        printf("        %s: %s is %.2f, needs %.2f\n", t->name, what, c, min);
        theme_failures++;
    }
}

static void test_registry(void)
{
    printf("== themes ==\n");
    check("four themes", theme_count() == 4);
    check("gruvbox is the default", strcmp(theme_name(0), "gruvbox") == 0);
    check("names are case-insensitive", theme_from_name("GruvBox") == 0);
    for (int i = 0; i < theme_count(); i++)
        if (theme_from_name(theme_name(i)) != i)
            check("a theme name does not round-trip", 0);
    check("every colour pair ID fits a 64-pair terminal", CP_LAST < 64);
}

static void test_contrast(void)
{
    printf("== contrast ==\n");
    check("black on white is the maximum, 21:1",
          theme_contrast(16, 231) > 20.9 && theme_contrast(16, 231) < 21.1);
    check("a colour against itself is 1:1", theme_contrast(100, 100) == 1.0);

    for (int i = 0; i < theme_count(); i++) {
        const Theme *t = theme_get(i);
        theme_failures = 0;

        need(t, "text",           t->fg,     t->bg, 7.0);
        need(t, "dim text",       t->dim,    t->bg, 3.0);
        need(t, "borders",        t->line,   t->bg, 1.4);
        need(t, "bar track",      t->track,  t->bg, 1.1);
        need(t, "shadow",         t->shadow, t->bg, 1.15);
        need(t, "ok text",        t->ok,     t->bg, 3.0);
        need(t, "error text",     t->err,    t->bg, 3.0);

        int acc[5] = { t->acc_board, t->acc_eval, t->acc_clock,
                       t->acc_moves, t->acc_engine };
        for (int k = 0; k < 5; k++) need(t, "panel title", acc[k], t->bg, 3.0);
        for (int k = 0; k < THEME_RAMP; k++)
            need(t, "gradient step", t->ramp[k], t->bg, 3.0);

        need(t, "light vs dark square", t->sq_light, t->sq_dark, 1.5);

        int under[8] = { t->sq_light, t->sq_dark, t->cursor, t->sel,
                         t->movehi, t->check, t->lm_light, t->lm_dark };
        for (int k = 0; k < 8; k++) {
            need(t, "white piece on a square", t->pc_white, under[k], 2.8);
            need(t, "black piece on a square", t->pc_black, under[k], 2.8);
        }

        char label[64];
        snprintf(label, sizeof(label), "%s: every combination is legible", t->name);
        check(label, theme_failures == 0);
    }
}

static void test_cli_names(void)
{
    printf("== --theme ==\n");
    CliArgs a;

    char *ok[] = { "dchess", "--theme", "catppuccin", NULL };
    memset(&a, 0, sizeof(a));
    check("a new theme name is accepted",
          cli_parse(3, ok, &a) == 0 && a.theme == theme_from_name("catppuccin"));

    char *old[] = { "dchess", "--theme", "classic", NULL };
    memset(&a, 0, sizeof(a));
    check("an old theme name is rejected", cli_parse(3, old, &a) != 0);
    check("and the error lists the new names", strstr(a.error_msg, "gruvbox") != NULL);
}

int main(void)
{
    test_registry();
    test_contrast();
    test_cli_names();

    if (failures) {
        printf("\n%d theme test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll theme tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make build/test_theme 2>&1 | grep -E 'error' | head -3`
Expected: errors for `theme_contrast`, `t->fg` and `CP_LAST`.

- [ ] **Step 3: Rewrite the theme module**

Replace `headers/utils/theme.h` from `typedef struct {` through `} Theme;` with:

```c
#define THEME_RAMP 8

/* All colours are xterm-256 palette indices, used directly: no
 * init_color(), so tmux and screen (which report can_change_color() false
 * while offering 256 colours) get the real theme. */
typedef struct {
    const char *name;
    int bg, fg, dim, line;
    int acc_board, acc_eval, acc_clock, acc_moves, acc_engine;
    int ramp[THEME_RAMP];            /* gradient, low to high */
    int track;                       /* empty part of a bar */
    int sq_light, sq_dark, pc_white, pc_black;
    int cursor, sel, movehi, check, lm_light, lm_dark;
    int ok, err, shadow;

    /* Terminals with fewer than 256 colours. */
    int fb_accent;
    int fb_cursor_bg;
    int fb_sel_bg;
    int fb_movehi_bg;
    int fb_check_bg;
} Theme;
```

and add after `int theme_from_name(const char *name);`:

```c
/* WCAG contrast ratio between two xterm-256 colours, 1.0 to 21.0. */
double theme_contrast(int a, int b);
```

Replace the `THEMES[]` table in `src/utils/theme.c` with:

```c
static const Theme THEMES[] = {
    { "gruvbox",
      235, 223, 245, 239,
      208, 142, 214, 175, 109,
      { 142, 142, 178, 214, 214, 208, 202, 167 }, 237,
      137, 94, 230, 234,
      66, 100, 65, 160, 136, 130,
      142, 167, 238,
      FB_YELLOW, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
    { "tokyonight",
      234, 189, 103, 238,
      111, 149, 179, 141, 117,
      { 149, 149, 150, 186, 179, 215, 210, 204 }, 236,
      103, 60, 231, 233,
      27, 64, 32, 204, 172, 130,
      149, 204, 237,
      FB_BLUE, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
    { "btop",
      16, 254, 245, 240,
      203, 83, 220, 75, 170,
      { 83, 83, 119, 190, 220, 214, 208, 203 }, 235,
      245, 241, 231, 16,
      25, 28, 30, 124, 136, 94,
      83, 203, 237,
      FB_GREEN, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
    { "catppuccin",
      235, 189, 103, 239,
      183, 151, 223, 218, 117,
      { 151, 151, 187, 223, 223, 217, 211, 211 }, 237,
      138, 96, 231, 234,
      31, 65, 32, 204, 172, 94,
      151, 211, 238,
      FB_MAGENTA, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
};
```

Add `#include <math.h>` to `src/utils/theme.c`, and at the end of the file:

```c
static void xterm_rgb(int i, int rgb[3])
{
    static const int SYS[16][3] = {
        {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},
        {0,128,128},{192,192,192},{128,128,128},{255,0,0},{0,255,0},
        {255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255},
    };
    static const int CUBE[6] = { 0, 95, 135, 175, 215, 255 };

    if (i < 16) {
        rgb[0] = SYS[i][0]; rgb[1] = SYS[i][1]; rgb[2] = SYS[i][2];
    } else if (i < 232) {
        i -= 16;
        rgb[0] = CUBE[i / 36]; rgb[1] = CUBE[(i / 6) % 6]; rgb[2] = CUBE[i % 6];
    } else {
        rgb[0] = rgb[1] = rgb[2] = 8 + 10 * (i - 232);
    }
}

static double channel(int v)
{
    double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double luminance(int i)
{
    int c[3];
    xterm_rgb(i, c);
    return 0.2126 * channel(c[0]) + 0.7152 * channel(c[1]) + 0.0722 * channel(c[2]);
}

double theme_contrast(int a, int b)
{
    double x = luminance(a), y = luminance(b);
    if (x < y) { double t = x; x = y; y = t; }
    return (x + 0.05) / (y + 0.05);
}
```

In `Makefile`, change `LDFLAGS = -lncursesw -pthread` to:

```make
LDFLAGS = -lncursesw -pthread -lm
```

- [ ] **Step 4: Rewrite the pair registry**

In `headers/tui/colors.h`:

- Change the comments on pairs 8, 10, 12 and 14 to say "white piece on …".
- Delete every `COL_*`, `SCP_*` and `SCOL_*` define, and the lines in the header comment that describe their ranges.
- After `#define CP_FRAME       37 ...` add:

```c
#define CP_ACC_BOARD   38
#define CP_ACC_EVAL    39
#define CP_ACC_CLOCK   40
#define CP_ACC_MOVES   41
#define CP_ACC_ENGINE  42
#define CP_TRACK       43
#define CP_RAMP_BASE   44   /* 44-51: gradient, low to high */

/* Black-piece counterparts of 8, 10, 12 and 14. The block-art pieces
 * share one silhouette, so colour is all that tells them apart. */
#define CP_CURSOR_PC_B  52
#define CP_SEL_PC_B     53
#define CP_MOVE_HI_PC_B 54
#define CP_CHECK_PC_B   55

#define CP_LAST         55

/* 8-colour terminals commonly allow only 64 pairs. */
_Static_assert(CP_LAST < 64, "colour pair IDs must stay below 64");
```

- Update the range table in the header comment to read `1 - 55  CP_*`.

- [ ] **Step 5: Rewrite colour initialisation**

In `src/tui/render.c`, replace the whole of `init_colors()` with:

```c
static void init_palette_256(const Theme *t)
{
    int bg = t->bg;

    init_pair(CP_LIGHT,   t->sq_light, t->sq_light);
    init_pair(CP_DARK,    t->sq_dark,  t->sq_dark);
    init_pair(CP_W_LIGHT, t->pc_white, t->sq_light);
    init_pair(CP_W_DARK,  t->pc_white, t->sq_dark);
    init_pair(CP_B_LIGHT, t->pc_black, t->sq_light);
    init_pair(CP_B_DARK,  t->pc_black, t->sq_dark);

    init_pair(CP_CURSOR,       t->pc_black, t->cursor);
    init_pair(CP_CURSOR_PC,    t->pc_white, t->cursor);
    init_pair(CP_CURSOR_PC_B,  t->pc_black, t->cursor);
    init_pair(CP_SEL,          t->pc_black, t->sel);
    init_pair(CP_SEL_PC,       t->pc_white, t->sel);
    init_pair(CP_SEL_PC_B,     t->pc_black, t->sel);
    init_pair(CP_MOVE_HI,      t->pc_black, t->movehi);
    init_pair(CP_MOVE_HI_PC,   t->pc_white, t->movehi);
    init_pair(CP_MOVE_HI_PC_B, t->pc_black, t->movehi);
    init_pair(CP_CHECK_SQ,     t->pc_black, t->check);
    init_pair(CP_CHECK_PC,     t->pc_white, t->check);
    init_pair(CP_CHECK_PC_B,   t->pc_black, t->check);

    init_pair(CP_LMVL,   t->lm_light, t->lm_light);
    init_pair(CP_LMVD,   t->lm_dark,  t->lm_dark);
    init_pair(CP_W_LMVL, t->pc_white, t->lm_light);
    init_pair(CP_W_LMVD, t->pc_white, t->lm_dark);
    init_pair(CP_B_LMVL, t->pc_black, t->lm_light);
    init_pair(CP_B_LMVD, t->pc_black, t->lm_dark);

    init_pair(CP_BORDER,     t->line, bg);
    init_pair(CP_TITLE,      t->fg,   bg);
    init_pair(CP_LINK,       t->dim,  bg);
    init_pair(CP_LABEL,      t->dim,  bg);
    init_pair(CP_INFO_HEAD,  t->acc_board, bg);
    init_pair(CP_INFO_VAL,   t->fg,   bg);
    init_pair(CP_STATUS_OK,  t->ok,   bg);
    init_pair(CP_STATUS_ERR, t->err,  bg);
    init_pair(CP_HINT,       t->dim,  bg);
    init_pair(CP_CMD,        t->fg,   bg);
    init_pair(CP_MOVE_W,     t->fg,   bg);
    init_pair(CP_MOVE_B,     t->dim,  bg);
    init_pair(CP_CAP_W,      t->fg,   bg);
    init_pair(CP_CAP_B,      t->dim,  bg);
    init_pair(CP_CANVAS,     t->fg,   bg);
    init_pair(CP_SHADOW,     t->shadow, bg);
    init_pair(CP_FRAME,      t->line, bg);

    init_pair(CP_ACC_BOARD,  t->acc_board,  bg);
    init_pair(CP_ACC_EVAL,   t->acc_eval,   bg);
    init_pair(CP_ACC_CLOCK,  t->acc_clock,  bg);
    init_pair(CP_ACC_MOVES,  t->acc_moves,  bg);
    init_pair(CP_ACC_ENGINE, t->acc_engine, bg);
    init_pair(CP_TRACK,      t->track, t->track);
    for (int i = 0; i < THEME_RAMP; i++)
        init_pair(CP_RAMP_BASE + i, t->ramp[i], bg);
}

static void init_palette_8(const Theme *t)
{
    static const int RAMP8[THEME_RAMP] = {
        COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_YELLOW,
        COLOR_YELLOW, COLOR_YELLOW, COLOR_RED, COLOR_RED,
    };

    init_pair(CP_LIGHT,      COLOR_BLACK,  COLOR_WHITE);
    init_pair(CP_DARK,       COLOR_WHITE,  COLOR_BLACK);
    init_pair(CP_W_LIGHT,    COLOR_YELLOW, COLOR_WHITE);
    init_pair(CP_W_DARK,     COLOR_YELLOW, COLOR_BLACK);
    init_pair(CP_B_LIGHT,    COLOR_RED,    COLOR_WHITE);
    init_pair(CP_B_DARK,     COLOR_RED,    COLOR_BLACK);
    init_pair(CP_CURSOR,       fb_plain_fg(t->fb_cursor_bg),    t->fb_cursor_bg);
    init_pair(CP_CURSOR_PC,    fb_white_pc_fg(t->fb_cursor_bg), t->fb_cursor_bg);
    init_pair(CP_CURSOR_PC_B,  fb_black_pc_fg(t->fb_cursor_bg), t->fb_cursor_bg);
    init_pair(CP_SEL,          fb_plain_fg(t->fb_sel_bg),       t->fb_sel_bg);
    init_pair(CP_SEL_PC,       fb_white_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_SEL_PC_B,     fb_black_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_MOVE_HI,      fb_plain_fg(t->fb_movehi_bg),    t->fb_movehi_bg);
    init_pair(CP_MOVE_HI_PC,   fb_white_pc_fg(t->fb_movehi_bg), t->fb_movehi_bg);
    init_pair(CP_MOVE_HI_PC_B, fb_black_pc_fg(t->fb_movehi_bg), t->fb_movehi_bg);
    init_pair(CP_CHECK_SQ,     fb_plain_fg(t->fb_check_bg),     t->fb_check_bg);
    init_pair(CP_CHECK_PC,     fb_white_pc_fg(t->fb_check_bg),  t->fb_check_bg);
    init_pair(CP_CHECK_PC_B,   fb_black_pc_fg(t->fb_check_bg),  t->fb_check_bg);
    init_pair(CP_LMVL,       fb_plain_fg(t->fb_sel_bg),       t->fb_sel_bg);
    init_pair(CP_LMVD,       fb_plain_fg(t->fb_sel_bg),       t->fb_sel_bg);
    init_pair(CP_W_LMVL,     fb_white_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_W_LMVD,     fb_white_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_B_LMVL,     fb_black_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_B_LMVD,     fb_black_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_BORDER,     t->fb_accent, -1);
    init_pair(CP_TITLE,      t->fb_accent, -1);
    init_pair(CP_LINK,       COLOR_WHITE,  -1);
    init_pair(CP_LABEL,      t->fb_accent, -1);
    init_pair(CP_INFO_HEAD,  COLOR_YELLOW, -1);
    init_pair(CP_INFO_VAL,   COLOR_WHITE,  -1);
    init_pair(CP_STATUS_OK,  COLOR_GREEN,  -1);
    init_pair(CP_STATUS_ERR, COLOR_RED,    -1);
    init_pair(CP_HINT,       t->fb_accent, -1);
    init_pair(CP_CMD,        COLOR_WHITE,  -1);
    init_pair(CP_MOVE_W,     COLOR_WHITE,  -1);
    init_pair(CP_MOVE_B,     t->fb_accent, -1);
    init_pair(CP_CAP_W,      COLOR_BLUE,   -1);
    init_pair(CP_CAP_B,      COLOR_RED,    -1);
    init_pair(CP_CANVAS,     COLOR_WHITE,  -1);
    init_pair(CP_SHADOW,     COLOR_BLACK,  -1);
    init_pair(CP_FRAME,      t->fb_accent, -1);
    init_pair(CP_ACC_BOARD,  COLOR_YELLOW,  -1);
    init_pair(CP_ACC_EVAL,   COLOR_GREEN,   -1);
    init_pair(CP_ACC_CLOCK,  COLOR_YELLOW,  -1);
    init_pair(CP_ACC_MOVES,  COLOR_MAGENTA, -1);
    init_pair(CP_ACC_ENGINE, COLOR_CYAN,    -1);
    init_pair(CP_TRACK,      COLOR_BLACK, COLOR_BLACK);
    for (int i = 0; i < THEME_RAMP; i++)
        init_pair(CP_RAMP_BASE + i, RAMP8[i], -1);
}

void init_colors(int theme)
{
    init_glyph_width();
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    const Theme *t = theme_get(theme);
    if (COLORS >= 256) init_palette_256(t);
    else               init_palette_8(t);

    bkgd(COLOR_PAIR(CP_CANVAS));
}
```

In `draw_board_grid()`, replace the four highlight branches that set `pc_attr`:

```c
                if      (is_check)  pc_attr = COLOR_PAIR(CP_CHECK_PC)   | A_BOLD;
                else if (is_sel)    pc_attr = COLOR_PAIR(CP_SEL_PC)     | A_BOLD;
                else if (is_cursor) pc_attr = COLOR_PAIR(CP_CURSOR_PC)  | A_BOLD;
                else if (is_movehi) pc_attr = COLOR_PAIR(CP_MOVE_HI_PC) | A_BOLD;
```

with:

```c
                if      (is_check)  pc_attr = COLOR_PAIR(iw ? CP_CHECK_PC   : CP_CHECK_PC_B)   | A_BOLD;
                else if (is_sel)    pc_attr = COLOR_PAIR(iw ? CP_SEL_PC     : CP_SEL_PC_B)     | A_BOLD;
                else if (is_cursor) pc_attr = COLOR_PAIR(iw ? CP_CURSOR_PC  : CP_CURSOR_PC_B)  | A_BOLD;
                else if (is_movehi) pc_attr = COLOR_PAIR(iw ? CP_MOVE_HI_PC : CP_MOVE_HI_PC_B) | A_BOLD;
```

- [ ] **Step 6: Put the stats screens on the theme's pairs**

In `src/tui/stats_tui.c`, delete the whole `init_stats_colors()` function, its `static int colors_inited` flag, and both `init_stats_colors();` calls. After the `#include` lines add:

```c
/* The statistics screens draw with the active theme's pairs. */
#define SCP_BORDER   CP_BORDER
#define SCP_TITLE    CP_ACC_BOARD
#define SCP_HEAD     CP_ACC_ENGINE
#define SCP_BAR_WIN  CP_STATUS_OK
#define SCP_BAR_LOSS CP_STATUS_ERR
#define SCP_BAR_DRAW CP_ACC_CLOCK
#define SCP_BAR_BG   CP_TRACK
#define SCP_VAL      CP_INFO_VAL
#define SCP_HINT     CP_HINT
#define SCP_LABEL    CP_INFO_VAL
#define SCP_GOOD     CP_STATUS_OK
#define SCP_BAD      CP_STATUS_ERR
#define SCP_NEUT     CP_ACC_CLOCK
#define SCP_GRAPH_AX CP_HINT
#define SCP_GRAPH_W  CP_STATUS_OK
#define SCP_GRAPH_L  CP_STATUS_ERR
#define SCP_GRAPH_D  CP_ACC_CLOCK
#define SCP_GRAPH_BG CP_TRACK
```

`show_stats_overlay()` runs for `dchess --stats` without going through the game, so it must set up colours itself. In `show_stats_overlay()`, directly after its `use_default_colors();` line, add:

```c
    init_colors(0);
```

and add `#include "tui/render.h"` to the includes.

- [ ] **Step 7: Theme backgrounds and names**

In `src/tui/tui.c`, `screen_build()`, after the four `newwin()` calls:

```c
    WINDOW *wins[] = { sc->board, sc->info, sc->eval_bar, sc->cmd };
    for (int i = 0; i < 4; i++)
        if (wins[i]) wbkgd(wins[i], COLOR_PAIR(CP_CANVAS));
```

In `src/tui/commands.c`, replace the unknown-theme message:

```c
            snprintf(state->status, sizeof(state->status),
                     "Unknown theme '%s'. Use: classic | midnight | forest | contrast",
                     cmd + 6);
```

with:

```c
            char names[96] = "";
            for (int i = 0; i < theme_count(); i++) {
                strncat(names, theme_name(i), sizeof(names) - strlen(names) - 1);
                if (i + 1 < theme_count())
                    strncat(names, " | ", sizeof(names) - strlen(names) - 1);
            }
            snprintf(state->status, sizeof(state->status),
                     "Unknown theme '%.40s'. Use: %s", cmd + 6, names);
```

In `src/utils/cli.c`, replace each of the three help-text occurrences of `classic | midnight | forest | contrast` with `gruvbox | tokyonight | btop | catppuccin`, and in the example line replace `dchess --theme midnight         Start with the midnight color theme` with `dchess --theme tokyonight       Start with the Tokyo Night theme`. Change `(default: classic)` to `(default: gruvbox)`.

- [ ] **Step 8: Run to verify it passes**

Run: `make clean >/dev/null; make 2>&1 | grep -cE 'error|warning'; make test 2>&1 | grep -E '^All |FAIL'`
Expected: `0`, then every suite passes, including `All theme tests passed.`

- [ ] **Step 9: Look at it under the terminal that used to fail**

Run:
```bash
tmux kill-session -t v 2>/dev/null
tmux new-session -d -s v -x 110 -y 34 "TERM=screen-256color ./dchess --no-menu"
sleep 2; tmux capture-pane -e -p -t v | grep -o '38;5;[0-9]*' | sort -u | head
tmux kill-session -t v
```
Expected: several `38;5;N` codes, i.e. 256-colour indices, where the old code emitted only 8-colour codes.

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "feat(tui): themes as 256-colour palettes

The RGB themes were applied only when can_change_color() was true.
tmux and screen report false while offering 256 colours, so there the
themes only ever rendered in the 8-colour fallback. Themes are now
xterm-256 indices used directly: Gruvbox (default), Tokyo Night, btop
and Catppuccin, replacing the old four.

Every colour was picked by measured contrast, and test_theme.c holds
each theme to it: 37 checks per theme, including both piece colours on
every square and highlight.

Pieces on check, selection, cursor and legal-move squares get separate
white and black pairs. They shared one, and since the block-art pieces
share a silhouette, a piece under the cursor could not be told apart as
White or Black.

The stats screens drop their private init_color palette, whose pair IDs
collided with the new gradient pairs, and use the theme's pairs.
All pair IDs stay below 64 for 8-colour terminals."
```

---

### Task 4: Dashboard maths

Pure functions for the layout and the graph, with no ncurses dependency, so the boundary cases in Review Focus can be tested.

**Files:**
- Create: `headers/utils/dash.h`, `src/utils/dash.c`
- Test: `tests/test_dash.c` (new)

**Interfaces:**
- Produces:
  - `#define DASH_EVAL_CLAMP 500`
  - `int dash_side_width(int cols);` → 0, 26 or 34
  - `typedef struct { int eval_h, clock_h, moves_h, engine_h; } DashSide;`
  - `DashSide dash_side_layout(int height);`
  - `int dash_graph_fill(int eval_cp, int rows);` → eighths filled, `0 .. rows*8`
  - `int dash_graph_start(int count, int width);` → first history index shown
  - `int dash_ramp_index(int level, int levels, int steps);` → `0 .. steps-1`
  - `int dash_bar_fill(long part, long whole, int width);` → cells filled
  - `void dash_count(long n, char *buf, size_t n_buf);` → `"999"`, `"1.5K"`, `"2.3M"`

- [ ] **Step 1: Write the failing test**

Create `tests/test_dash.c`:

```c
/* Dashboard layout and graph maths.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "utils/dash.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void test_side_width(void)
{
    printf("== side column width ==\n");
    check("hidden below 60 columns",  dash_side_width(59) == 0);
    check("26 wide at exactly 60",    dash_side_width(60) == 26);
    check("26 wide at 109",           dash_side_width(109) == 26);
    check("34 wide from 110",         dash_side_width(110) == 34);
    check("hidden at the 34-column minimum", dash_side_width(34) == 0);
}

static void test_side_layout(void)
{
    printf("== side column layout ==\n");

    DashSide tall = dash_side_layout(40);
    check("tall: every panel shown",
          tall.eval_h == 6 && tall.clock_h == 6 && tall.engine_h == 5);
    check("tall: moves take the rest", tall.moves_h == 40 - 17);

    DashSide mid = dash_side_layout(18);
    check("short: the eval graph shrinks first", mid.eval_h == 4);
    check("short: the engine panel goes next", mid.engine_h == 0);
    check("short: moves still have room", mid.moves_h >= 3);

    for (int h = 0; h <= 60; h++) {
        DashSide s = dash_side_layout(h);
        int sum = s.eval_h + s.clock_h + s.moves_h + s.engine_h;
        if (sum > h || s.moves_h < 0) {
            check("no layout ever overflows its height", 0);
            return;
        }
    }
    check("no layout ever overflows its height", 1);
}

static void test_graph(void)
{
    printf("== eval graph ==\n");
    check("level is half height",        dash_graph_fill(0, 3) == 12);
    check("+5 pawns fills the column",   dash_graph_fill(500, 3) == 24);
    check("-5 pawns empties it",         dash_graph_fill(-500, 3) == 0);
    check("a mate score clamps to full", dash_graph_fill(999000, 3) == 24);
    check("a mated score clamps to empty", dash_graph_fill(-999000, 3) == 0);
    check("+1 pawn is above half",       dash_graph_fill(100, 3) > 12);
    check("no rows, no fill",            dash_graph_fill(300, 0) == 0);

    check("a short game starts at the beginning", dash_graph_start(5, 20) == 0);
    check("a long game shows the newest",         dash_graph_start(50, 20) == 30);
    check("an empty history starts at 0",         dash_graph_start(0, 20) == 0);
}

static void test_ramp_and_bars(void)
{
    printf("== gradient and bars ==\n");
    check("bottom level is the first colour",   dash_ramp_index(0, 3, 8) == 0);
    check("top level is the last colour",       dash_ramp_index(2, 3, 8) == 7);
    check("a single level uses the first colour", dash_ramp_index(0, 1, 8) == 0);
    check("out-of-range levels are clamped",    dash_ramp_index(9, 3, 8) == 7);

    check("half of the time fills half the bar", dash_bar_fill(50, 100, 20) == 10);
    check("all of it fills the bar",            dash_bar_fill(100, 100, 20) == 20);
    check("no time spent yet: empty bars",      dash_bar_fill(0, 0, 20) == 0);
    check("nothing spent by this side: empty",  dash_bar_fill(0, 100, 20) == 0);
}

static void test_count(void)
{
    printf("== counts ==\n");
    char b[16];
    dash_count(999, b, sizeof(b));     check("999 stays 999",  strcmp(b, "999") == 0);
    dash_count(1500, b, sizeof(b));    check("1500 is 1.5K",   strcmp(b, "1.5K") == 0);
    dash_count(2300000, b, sizeof(b)); check("2300000 is 2.3M", strcmp(b, "2.3M") == 0);
}

int main(void)
{
    test_side_width();
    test_side_layout();
    test_graph();
    test_ramp_and_bars();
    test_count();

    if (failures) {
        printf("\n%d dashboard test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll dashboard tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make build/test_dash 2>&1 | grep -E 'error' | head -3`
Expected: `utils/dash.h: No such file or directory`.

- [ ] **Step 3: Implement**

Create `headers/utils/dash.h`:

```c
#ifndef DASH_H
#define DASH_H

#include <stddef.h>

/* Layout and graph maths for the dashboard. No ncurses, so it can be
 * tested; tui/panels.c does the drawing. */

#define DASH_EVAL_CLAMP 500   /* centipawns that fill or empty the graph */

typedef struct { int eval_h, clock_h, moves_h, engine_h; } DashSide;

int      dash_side_width(int cols);
DashSide dash_side_layout(int height);

/* Eighths of a column to fill: -CLAMP is empty, 0 half, +CLAMP full. */
int dash_graph_fill(int eval_cp, int rows);

/* First history index shown when `count` values must fit `width` columns. */
int dash_graph_start(int count, int width);

int  dash_ramp_index(int level, int levels, int steps);
int  dash_bar_fill(long part, long whole, int width);
void dash_count(long n, char *buf, size_t n_buf);

#endif
```

Create `src/utils/dash.c`:

```c
#include "utils/dash.h"
#include <stdio.h>

#define EVAL_H   6
#define EVAL_MIN 4
#define CLOCK_H  6
#define ENGINE_H 5
#define MOVES_MIN 3

int dash_side_width(int cols)
{
    if (cols < 60)  return 0;
    if (cols < 110) return 26;
    return 34;
}

/* Moves take whatever is left. Under pressure the graph shrinks first,
 * then the engine panel goes, then the clocks. */
DashSide dash_side_layout(int height)
{
    DashSide s = { EVAL_H, CLOCK_H, 0, ENGINE_H };

    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.eval_h = EVAL_MIN;
    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.engine_h = 0;
    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.clock_h = 0;
    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.eval_h = 0;

    s.moves_h = height - (s.eval_h + s.clock_h + s.engine_h);
    if (s.moves_h < 0) s.moves_h = 0;
    return s;
}

int dash_graph_fill(int eval_cp, int rows)
{
    if (rows <= 0) return 0;
    if (eval_cp >  DASH_EVAL_CLAMP) eval_cp =  DASH_EVAL_CLAMP;
    if (eval_cp < -DASH_EVAL_CLAMP) eval_cp = -DASH_EVAL_CLAMP;

    long total = rows * 8L;
    long num = (long)(eval_cp + DASH_EVAL_CLAMP) * total;
    long den = 2L * DASH_EVAL_CLAMP;
    return (int)((num + den / 2) / den);
}

int dash_graph_start(int count, int width)
{
    if (width <= 0 || count <= width) return 0;
    return count - width;
}

int dash_ramp_index(int level, int levels, int steps)
{
    if (levels <= 1 || steps <= 1) return 0;
    if (level < 0)       level = 0;
    if (level >= levels) level = levels - 1;
    return level * (steps - 1) / (levels - 1);
}

int dash_bar_fill(long part, long whole, int width)
{
    if (whole <= 0 || width <= 0 || part <= 0) return 0;
    if (part >= whole) return width;
    return (int)((part * width + whole / 2) / whole);
}

void dash_count(long n, char *buf, size_t n_buf)
{
    if      (n >= 1000000) snprintf(buf, n_buf, "%.1fM", n / 1000000.0);
    else if (n >= 1000)    snprintf(buf, n_buf, "%.1fK", n / 1000.0);
    else                   snprintf(buf, n_buf, "%ld", n);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `make test 2>&1 | grep -E '^All |FAIL'`
Expected: every suite passes, including `All dashboard tests passed.`

- [ ] **Step 5: Commit**

```bash
git add headers/utils/dash.h src/utils/dash.c tests/test_dash.c
git commit -m "feat: layout and graph maths for the dashboard

Pure functions, no ncurses, so the edge cases are testable: when the
side column appears and how wide, how panels give way on a short
terminal, how an evaluation maps onto the graph (mate scores clamp),
and which evaluations a long game shows."
```

---

### Task 5: Dashboard layout

Replaces the info panel, eval bar, clock row and five-line status block with layout A: the board in a framed panel on the left, a side column of eval, clocks, moves and engine panels, and a command bar. Eval and clocks show their values as text here; Task 6 adds the graph and bars.

**Files:**
- Create: `headers/tui/panels.h`, `src/tui/panels.c`
- Modify: `src/tui/render.c`, `headers/tui/render.h`
- Modify: `src/tui/tui.c`, `headers/tui/tui.h`
- Modify: `src/tui/commands.c`, `src/tui/input.c`

**Interfaces:**
- Consumes: `CP_ACC_*` (Task 3), `dash_side_width`, `dash_side_layout`, `dash_count` (Task 4), `SearchResult.elapsed_ms` (Task 2).
- Produces:
  - `void panel_frame(WINDOW *win, const char *title, int accent_pair);`
  - `void draw_side_column(WINDOW *side, const TUIState *state);`
  - `void draw_command_bar(WINDOW *cmd, const TUIState *state);`
  - `void mvw_clip(WINDOW *win, int row, int col, const char *fmt, ...);` (was static in `render.c`)
  - `void render_all(WINDOW *board, WINDOW *side, WINDOW *cmd, const TUIState *state);`
  - `TUIState.last_search` (`SearchResult`, `nodes == 0` until the first search)

- [ ] **Step 1: Keep the last search in the state**

In `headers/tui/tui.h`, after `char last_eval[32];`:

```c
    SearchResult last_search;   /* nodes == 0 until the first search */
```

In `src/tui/commands.c`, `apply_engine_result()`, immediately after `game_record_eval(&state->game, score_white);`:

```c
    state->last_search = res;
```

In `tui_new_game()`, after `game_reset(&state->game);`:

```c
    memset(&state->last_search, 0, sizeof(state->last_search));
```

- [ ] **Step 2: Share the clipped printer**

In `src/tui/render.c`, change `static void mvw_clip(` to `void mvw_clip(`. In `headers/tui/render.h`, replace the `render_all` declaration with:

```c
void mvw_clip(WINDOW *win, int row, int col, const char *fmt, ...);

void render_all(WINDOW *board, WINDOW *side, WINDOW *cmd, const TUIState *state);
```

- [ ] **Step 3: Create the panels**

Create `headers/tui/panels.h`:

```c
#ifndef TUI_PANELS_H
#define TUI_PANELS_H

#include <ncurses.h>
#include "tui/tui.h"

/* Rounded border with `title` set into the top edge in `accent_pair`. */
void panel_frame(WINDOW *win, const char *title, int accent_pair);

void draw_side_column(WINDOW *side, const TUIState *state);

/* Frame and status line; read_key() draws the input row. */
void draw_command_bar(WINDOW *cmd, const TUIState *state);

#endif
```

Create `src/tui/panels.c`:

```c
#include "tui/panels.h"
#include "tui/render.h"
#include "tui/colors.h"
#include "utils/dash.h"
#include "utils/constants.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <wchar.h>

static void put_wc(WINDOW *w, int row, int col, wchar_t ch, int pair, attr_t attr)
{
    cchar_t cc;
    wchar_t s[2] = { ch, L'\0' };
    setcchar(&cc, s, attr, (short)pair, NULL);
    mvwadd_wch(w, row, col, &cc);
}

void panel_frame(WINDOW *win, const char *title, int accent_pair)
{
    int h, w;
    getmaxyx(win, h, w);
    if (h < 2 || w < 2) return;

    for (int c = 1; c < w - 1; c++) {
        put_wc(win, 0,     c, L'─', CP_BORDER, 0);
        put_wc(win, h - 1, c, L'─', CP_BORDER, 0);
    }
    for (int r = 1; r < h - 1; r++) {
        put_wc(win, r, 0,     L'│', CP_BORDER, 0);
        put_wc(win, r, w - 1, L'│', CP_BORDER, 0);
    }
    put_wc(win, 0,     0,     L'╭', CP_BORDER, 0);
    put_wc(win, 0,     w - 1, L'╮', CP_BORDER, 0);
    put_wc(win, h - 1, 0,     L'╰', CP_BORDER, 0);
    put_wc(win, h - 1, w - 1, L'╯', CP_BORDER, 0);

    if (title && w > 6) {
        wattron(win, COLOR_PAIR(accent_pair) | A_BOLD);
        mvw_clip(win, 0, 2, " %s ", title);
        wattroff(win, COLOR_PAIR(accent_pair) | A_BOLD);
    }
}

/* A sub-window over rows [row, row+h) of the side column. */
static WINDOW *sub(WINDOW *side, int row, int h)
{
    int sh, sw;
    getmaxyx(side, sh, sw);
    (void)sh;
    WINDOW *p = derwin(side, h, sw, row, 0);
    if (p) wbkgd(p, COLOR_PAIR(CP_CANVAS));
    return p;
}

static void clock_cs(const TUIState *s, long *w_cs, long *b_cs)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long run = (now.tv_sec  - s->game.turn_start_mono.tv_sec)  * 100
             + (now.tv_nsec - s->game.turn_start_mono.tv_nsec) / 10000000;
    if (run < 0) run = 0;

    *w_cs = (long)s->game.white_clock * 100;
    *b_cs = (long)s->game.black_clock * 100;
    if (!s->game.game_over && s->game.clock_started) {
        if (s->game.clock_side == WHITE) *w_cs += run;
        else                             *b_cs += run;
    }
}

static void fmt_clock(long cs, char *out, size_t n)
{
    if (cs < 0) cs = 0;
    long m = cs / 6000, s = (cs % 6000) / 100, c = cs % 100;
    if (m > 99) { m = 99; s = 59; c = 99; }
    snprintf(out, n, "%02ld:%02ld.%02ld", m, s, c);
}

static void draw_eval_panel(WINDOW *p, const TUIState *state)
{
    panel_frame(p, "eval", CP_ACC_EVAL);
    int h, w;
    getmaxyx(p, h, w);

    float v = 0.0f;
    sscanf(state->last_eval, "%f", &v);
    const char *who = v > 0.05f ? "white better" : v < -0.05f ? "black better" : "level";

    wattron(p, COLOR_PAIR(CP_ACC_EVAL) | A_BOLD);
    mvw_clip(p, h - 2, 2, "%s", state->last_eval);
    wattroff(p, COLOR_PAIR(CP_ACC_EVAL) | A_BOLD);
    wattron(p, COLOR_PAIR(CP_HINT));
    mvw_clip(p, h - 2, w - 2 - (int)strlen(who), "%s", who);
    wattroff(p, COLOR_PAIR(CP_HINT));
}

static void draw_clock_panel(WINDOW *p, const TUIState *state)
{
    panel_frame(p, "clocks", CP_ACC_CLOCK);
    int h, w;
    getmaxyx(p, h, w);
    (void)h;

    long w_cs, b_cs;
    clock_cs(state, &w_cs, &b_cs);

    const struct { const char *label; long cs; int side; int row; } rows[2] = {
        { "W", w_cs, WHITE, 1 }, { "B", b_cs, BLACK, 3 },
    };
    for (int i = 0; i < 2; i++) {
        char t[16];
        fmt_clock(rows[i].cs, t, sizeof(t));
        int active = !state->game.game_over && state->game.clock_side == rows[i].side;
        attr_t a = active ? (COLOR_PAIR(CP_ACC_CLOCK) | A_BOLD) : COLOR_PAIR(CP_INFO_VAL);
        wattron(p, a);
        mvw_clip(p, rows[i].row, 2, "%s", rows[i].label);
        mvw_clip(p, rows[i].row, w - 2 - (int)strlen(t), "%s", t);
        wattroff(p, a);
    }
}

static void draw_moves_panel(WINDOW *p, const TUIState *state)
{
    panel_frame(p, "moves", CP_ACC_MOVES);
    int h, w;
    getmaxyx(p, h, w);
    (void)w;

    const GameState *g = &state->game;
    int total = (g->move_count + 1) / 2;
    if (total == 0) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, "no moves yet");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

    int rows  = h - 2;
    int first = total > rows ? total - rows : 0;
    for (int m = first; m < total; m++) {
        int r = 1 + (m - first);
        int wi = 2 * m, bi = 2 * m + 1;

        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, r, 1, "%3d.", m + 1);
        wattroff(p, COLOR_PAIR(CP_HINT));

        attr_t wa = (wi == g->move_count - 1) ? (COLOR_PAIR(CP_ACC_MOVES) | A_BOLD)
                                              : COLOR_PAIR(CP_INFO_VAL);
        wattron(p, wa);
        mvw_clip(p, r, 6, "%s", g->move_history[wi]);
        wattroff(p, wa);

        if (bi < g->move_count) {
            attr_t ba = (bi == g->move_count - 1) ? (COLOR_PAIR(CP_ACC_MOVES) | A_BOLD)
                                                  : COLOR_PAIR(CP_INFO_VAL);
            wattron(p, ba);
            mvw_clip(p, r, 14, "%s", g->move_history[bi]);
            wattroff(p, ba);
        }
    }
}

static void engine_row(WINDOW *p, int row, const char *label, const char *value)
{
    int h, w;
    getmaxyx(p, h, w);
    (void)h;
    wattron(p, COLOR_PAIR(CP_HINT));
    mvw_clip(p, row, 2, "%s", label);
    wattroff(p, COLOR_PAIR(CP_HINT));
    wattron(p, COLOR_PAIR(CP_INFO_VAL));
    mvw_clip(p, row, w - 2 - (int)strlen(value), "%s", value);
    wattroff(p, COLOR_PAIR(CP_INFO_VAL));
}

static void draw_engine_panel(WINDOW *p, const TUIState *state)
{
    panel_frame(p, "engine", CP_ACC_ENGINE);

    if (state->two_player || state->engine_side < 0) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, "no engine");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }
    if (state->search_running) {
        wattron(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        mvw_clip(p, 1, 2, "thinking...");
        wattroff(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        return;
    }

    const SearchResult *r = &state->last_search;
    if (r->nodes == 0) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, "waiting");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

    char depth[16], nodes[16], nps[16];
    snprintf(depth, sizeof(depth), "%d", r->depth_reached);
    dash_count(r->nodes, nodes, sizeof(nodes));
    long ms = r->elapsed_ms > 0 ? r->elapsed_ms : 1;
    dash_count(r->nodes * 1000 / ms, nps, sizeof(nps));

    engine_row(p, 1, "depth", depth);
    engine_row(p, 2, "nodes", nodes);
    engine_row(p, 3, "nps",   nps);
}

void draw_side_column(WINDOW *side, const TUIState *state)
{
    if (!side) return;
    werase(side);

    int h, w;
    getmaxyx(side, h, w);
    (void)w;
    DashSide L = dash_side_layout(h);

    struct { int h; void (*draw)(WINDOW *, const TUIState *); } panes[4] = {
        { L.eval_h,   draw_eval_panel   },
        { L.clock_h,  draw_clock_panel  },
        { L.moves_h,  draw_moves_panel  },
        { L.engine_h, draw_engine_panel },
    };

    int row = 0;
    for (int i = 0; i < 4; i++) {
        if (panes[i].h <= 0) continue;
        WINDOW *p = sub(side, row, panes[i].h);
        if (p) {
            panes[i].draw(p, state);
            delwin(p);
        }
        row += panes[i].h;
    }
    wnoutrefresh(side);
}

void draw_command_bar(WINDOW *cmd, const TUIState *state)
{
    werase(cmd);
    panel_frame(cmd, "command", CP_ACC_BOARD);
    int h, w;
    getmaxyx(cmd, h, w);
    (void)h;

    int is_err = strncmp(state->status, "Illegal", 7) == 0 ||
                 strncmp(state->status, "Bad",     3) == 0 ||
                 strncmp(state->status, "Unknown", 7) == 0;
    const char *hints = "i type  u undo  tab stats";
    int room = w - 4;
    int show_hints = room > (int)strlen(state->status) + (int)strlen(hints) + 3;

    attr_t a = is_err ? (COLOR_PAIR(CP_STATUS_ERR) | A_BOLD)
                      : (COLOR_PAIR(CP_STATUS_OK)  | A_BOLD);
    wattron(cmd, a);
    mvw_clip(cmd, 2, 2, "%s", state->status);
    wattroff(cmd, a);

    if (show_hints) {
        wattron(cmd, COLOR_PAIR(CP_HINT));
        mvw_clip(cmd, 2, w - 2 - (int)strlen(hints), "%s", hints);
        wattroff(cmd, COLOR_PAIR(CP_HINT));
    }
    wnoutrefresh(cmd);
}
```

- [ ] **Step 4: Put the board in its panel**

In `src/tui/render.c`:

1. Delete `draw_status()`, `draw_info()`, `draw_cmd()`, `draw_eval_bar()`, `format_clock()` with its two `CLOCK_*` defines, and `draw_board_frame()`. Also delete the helpers only `draw_info()` used: `draw_captured_row()`, `captured_counts()`, `popcount64()`, and the `START_CNT` and `PC_VAL` tables. Keep `piece_at()`, `hfill()`, `put_glyph()` and `parse_last_move()`: the board still uses them.
2. In `fit_board()`, delete the `framed` parameter and use `chrome_h = 1`, `chrome_w = 2`.
3. In `draw_board_grid()`, delete the `framed` parameter and the `if (framed) draw_board_frame(...)` call, use `start_col - 2` for the rank label column, and `start_row + 8 * sq_h` for the file-label row.
4. Replace the whole of `draw_board()` with:

```c
static void draw_board(WINDOW *win, const TUIState *state)
{
    int wh, ww;
    getmaxyx(win, wh, ww);

    int avail_h = wh - 2;   /* inside the panel border */
    int avail_w = ww - 2;

    int sq_h, sq_w;
    if (!fit_board(avail_h, avail_w, &sq_h, &sq_w)) {
        sq_h = 1;
        sq_w = GLYPH_W + 2;
    }

    int board_h = 8 * sq_h + 1;   /* + file labels */
    int board_w = 8 * sq_w + 2;   /* + rank labels */
    int sr = 1 + (avail_h - board_h) / 2;
    int sc = 1 + (avail_w - board_w) / 2 + 2;
    if (sr < 1) sr = 1;
    if (sc < 3) sc = 3;

    draw_board_grid(win, state, sr, sc, sq_h, sq_w);
}
```

5. Replace the whole of `render_all()` with:

```c
void render_all(WINDOW *board, WINDOW *side, WINDOW *cmd, const TUIState *state)
{
    werase(board);
    panel_frame(board, "dchess", CP_ACC_BOARD);

    int bh, bw;
    getmaxyx(board, bh, bw);
    const char *brand = " github.com/DDumbying ";
    if (bw > (int)strlen(brand) + 12) {
        wattron(board, COLOR_PAIR(CP_LINK));
        mvw_clip(board, 0, bw - (int)strlen(brand) - 2, "%s", brand);
        wattroff(board, COLOR_PAIR(CP_LINK));
    }
    if (is_in_check(&state->game.pos, state->game.pos.side)) {
        wattron(board, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        mvw_clip(board, bh - 1, bw - 10, " CHECK ");
        wattroff(board, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
    }

    draw_board(board, state);
    wnoutrefresh(board);

    draw_side_column(side, state);
    draw_command_bar(cmd, state);
}
```

6. Add `#include "tui/panels.h"` to `render.c`'s includes.

- [ ] **Step 5: Rebuild the screen as three windows**

In `src/tui/tui.c`:

- Change `struct Screen` to:

```c
struct Screen {
    WINDOW   *board, *side, *cmd;
    TUIState *state;
};
```

- Add `#include "utils/dash.h"` to the includes.
- Replace the body of `screen_build()` with:

```c
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    const int cmd_h = 4;
    int main_h  = rows - cmd_h;
    int side_w  = dash_side_width(cols);
    int board_w = cols - side_w;

    sc->board = newwin(main_h, board_w, 0, 0);
    sc->side  = side_w ? newwin(main_h, side_w, 0, board_w) : NULL;
    sc->cmd   = newwin(cmd_h, cols, main_h, 0);

    WINDOW *wins[] = { sc->board, sc->side, sc->cmd };
    for (int i = 0; i < 3; i++)
        if (wins[i]) wbkgd(wins[i], COLOR_PAIR(CP_CANVAS));

    werase(stdscr);
    wrefresh(stdscr);

    keypad(sc->board, TRUE);
    keypad(sc->cmd,   TRUE);
    wtimeout(sc->cmd, 100);
```

- Replace `screen_free_windows()`'s body with:

```c
    if (sc->board) delwin(sc->board);
    if (sc->side)  delwin(sc->side);
    if (sc->cmd)   delwin(sc->cmd);
    sc->board = sc->side = sc->cmd = NULL;
```

- In `screen_paint()`, change the window list and the render call to:

```c
    WINDOW *wins[] = { stdscr, sc->board, sc->side, sc->cmd };
```

```c
    render_all(sc->board, sc->side, sc->cmd, sc->state);
```

- [ ] **Step 6: Theme the input row**

In `src/tui/input.c`, add `#include "tui/colors.h"`, then replace the two `COLOR_PAIR(7)` uses:

```c
    wattron(win, COLOR_PAIR(7));
```
```c
    wattroff(win, COLOR_PAIR(7));
```

with:

```c
    wattron(win, COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
```
```c
    wattroff(win, COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
```

`COLOR_PAIR(7)` was a hard-coded `CP_CURSOR`, which bypassed the registry.

- [ ] **Step 7: Build and test**

Run: `make clean >/dev/null; make 2>&1 | grep -cE 'error|warning'; make test 2>&1 | grep -E '^All |FAIL'`
Expected: `0`, then every suite passes.

- [ ] **Step 8: Look at it**

Run:
```bash
for sz in "120 40" "80 24" "60 20" "34 20"; do set -- $sz
  tmux kill-session -t v 2>/dev/null
  tmux new-session -d -s v -x $1 -y $2 "./dchess --no-menu -d easy"
  sleep 2; for m in e2e4 g1f3; do tmux send-keys -t v "i$m" Enter; sleep 2.5; done
  echo "=== ${1}x${2} ==="; tmux capture-pane -p -t v; tmux kill-session -t v
done
```
Expected: at 120x40 and 80x24, the board panel on the left and eval, clocks, moves and engine panels on the right, with no text crossing a border. At 60x20, the side column is present with panels dropped in order. At 34x20, the board alone with the command bar. In every size the moves panel lists `e4` and the engine's replies in SAN, and the engine panel shows depth, nodes and nps.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat(tui): dashboard layout

The board moves into a framed panel on the left and a side column holds
eval, clocks, moves and engine panels, replacing the info panel, the
eval bar, the clock row and the five-line status block. Status and key
hints move into a command bar at the bottom.

The engine panel reads the last search's depth, nodes and speed. The
input row stops using a hard-coded pair number that bypassed the colour
registry."
```

---

### Task 6: Eval graph and clock bars

**Files:**
- Modify: `src/tui/panels.c`

**Interfaces:**
- Consumes: `dash_graph_fill`, `dash_graph_start`, `dash_ramp_index`, `dash_bar_fill` (Task 4); `CP_RAMP_BASE`, `CP_TRACK`, `THEME_RAMP` (Task 3); `eval_history` in White's view (Task 1).

- [ ] **Step 1: Draw the graph**

In `src/tui/panels.c`, add `#include "utils/theme.h"`, and before `draw_eval_panel()` add:

```c
static void draw_eval_graph(WINDOW *p, int top, int left, int rows, int cols,
                            const GameState *g)
{
    static const wchar_t BLOCKS[9] = {
        L' ', L'▁', L'▂', L'▃', L'▄', L'▅', L'▆', L'▇', L'█',
    };

    int zero = top + rows - 1 - rows / 2;
    for (int c = 0; c < cols; c++)
        put_wc(p, zero, left + c, L'─', CP_HINT, 0);

    int start = dash_graph_start(g->eval_count, cols);
    for (int c = 0; c < cols && start + c < g->eval_count; c++) {
        int fill = dash_graph_fill(g->eval_history[start + c], rows);
        for (int r = 0; r < rows; r++) {          /* r = 0 is the bottom row */
            int cell = fill - r * 8;
            if (cell <= 0) continue;
            if (cell > 8) cell = 8;
            int pair = CP_RAMP_BASE + dash_ramp_index(r, rows, THEME_RAMP);
            put_wc(p, top + rows - 1 - r, left + c, BLOCKS[cell], pair, 0);
        }
    }
}
```

In `draw_eval_panel()`, after `getmaxyx(p, h, w);`, add:

```c
    int rows = h - 3;   /* border top and bottom, and the value line */
    int cols = w - 4;
    if (rows >= 1 && cols >= 1)
        draw_eval_graph(p, 1, 2, rows, cols, &state->game);
```

- [ ] **Step 2: Draw the bars**

Before `draw_clock_panel()` add:

```c
static void draw_bar(WINDOW *p, int row, int col, int width, int fill)
{
    for (int i = 0; i < width; i++) {
        if (i < fill) {
            int pair = CP_RAMP_BASE + dash_ramp_index(i, width, THEME_RAMP);
            put_wc(p, row, col + i, L'█', pair, 0);
        } else {
            put_wc(p, row, col + i, L' ', CP_TRACK, 0);
        }
    }
}
```

In `draw_clock_panel()`, after the `for` loop that prints the labels and times, add:

```c
    long total = w_cs + b_cs;
    int  bar_w = w - 4;
    draw_bar(p, 2, 2, bar_w, dash_bar_fill(w_cs, total, bar_w));
    draw_bar(p, 4, 2, bar_w, dash_bar_fill(b_cs, total, bar_w));
```

- [ ] **Step 3: Build and test**

Run: `make 2>&1 | grep -cE 'error|warning'; make test 2>&1 | grep -E '^All |FAIL'`
Expected: `0`, then every suite passes.

- [ ] **Step 4: Look at it**

Run:
```bash
tmux kill-session -t v 2>/dev/null
tmux new-session -d -s v -x 120 -y 40 "./dchess --no-menu -d easy"
sleep 2; for m in e2e4 g1f3 f1c4 d2d4 e1g1; do tmux send-keys -t v "i$m" Enter; sleep 2.5; done
tmux capture-pane -p -t v | cut -c80-120 | head -20; tmux kill-session -t v
```
Expected: the eval panel shows one block column per engine move with a `─` rule at half height; the clocks panel shows two bars whose lengths split the elapsed time between the sides.

- [ ] **Step 5: Commit**

```bash
git add src/tui/panels.c
git commit -m "feat(tui): eval graph and clock bars

The eval panel graphs the whole game in eighth-block columns, coloured
along the theme's gradient, with a rule marking level. Each clock gets a
bar showing its side's share of the thinking time -- there is no time
control to count down against, so share is the honest measure."
```

---

### Task 7: Restyle onboarding, the game-over popup and statistics

**Files:**
- Modify: `src/tui/onboard.c`, `src/tui/tui.c` (`build_game_over_panel`), `src/tui/stats_tui.c`

**Interfaces:**
- Consumes: `panel_frame()` (Task 5), `CP_CANVAS` and `CP_ACC_*` (Task 3).

- [ ] **Step 1: Onboarding**

In `src/tui/onboard.c`, add `#include "tui/panels.h"`. After `win = newwin(ph, pw, pr, pc);` add:

```c
            wbkgd(win, COLOR_PAIR(CP_CANVAS));
```

Replace the border-and-title block at the top of the draw loop:

```c
        wattron(win, COLOR_PAIR(CP_BORDER));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(CP_BORDER));

        wattron(win, COLOR_PAIR(CP_TITLE) | A_BOLD);
        const char *title = " dchess -- New Game ";
        int title_col = (pw - (int)strlen(title)) / 2;
        if (title_col < 1) title_col = 1;
        mvwprintw(win, 0, title_col, "%s", title);
        wattroff(win, COLOR_PAIR(CP_TITLE) | A_BOLD);
```

with:

```c
        panel_frame(win, "dchess · new game", CP_ACC_BOARD);
```

- [ ] **Step 2: Game-over popup**

In `src/tui/tui.c`, `build_game_over_panel()`, after `WINDOW *pop = newwin(ph, pw, pr, pc_col);` add:

```c
    wbkgd(pop, COLOR_PAIR(CP_CANVAS));
```

and replace:

```c
    wattron(pop, COLOR_PAIR(CP_BORDER));
    box(pop, ACS_VLINE, ACS_HLINE);
    wattroff(pop, COLOR_PAIR(CP_BORDER));

    wattron(pop, COLOR_PAIR(CP_TITLE)|A_BOLD);
    mvwprintw(pop, 0, (pw-11)/2, " GAME OVER ");
    wattroff(pop, COLOR_PAIR(CP_TITLE)|A_BOLD);
```

with:

```c
    panel_frame(pop, "game over", CP_ACC_BOARD);
```

Add `#include "tui/panels.h"` to `tui.c`.

- [ ] **Step 3: Statistics**

In `src/tui/stats_tui.c`, add `#include "tui/panels.h"`. In `draw_stats_mini()`, after `WINDOW *pop = newwin(pop_h, pop_w, pop_r, pop_c);` add `wbkgd(pop, COLOR_PAIR(CP_CANVAS));`, then replace:

```c
    wattron(pop, COLOR_PAIR(SCP_BORDER));
    box(pop, ACS_VLINE, ACS_HLINE);
    wattroff(pop, COLOR_PAIR(SCP_BORDER));

    /* Title */
    wattron(pop, COLOR_PAIR(SCP_TITLE) | A_BOLD);
    const char *title = " Statistics ";
    mvwprintw(pop, 0, (pop_w - (int)strlen(title)) / 2, "%s", title);
    wattroff(pop, COLOR_PAIR(SCP_TITLE) | A_BOLD);
```

with:

```c
    panel_frame(pop, "statistics", CP_ACC_ENGINE);
```

Leave the `" any key to close "` hint that follows as it is.

In `draw_stats_overlay()`, replace:

```c
    wattron(win, COLOR_PAIR(SCP_BORDER));
    box(win, ACS_VLINE, ACS_HLINE);
    wattroff(win, COLOR_PAIR(SCP_BORDER));

    wattron(win, COLOR_PAIR(SCP_TITLE) | A_BOLD);
    const char *title = " dchess — Statistics ";
    mvwprintw(win, 0, (ww - (int)strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(SCP_TITLE) | A_BOLD);
```

with:

```c
    wbkgd(win, COLOR_PAIR(CP_CANVAS));
    panel_frame(win, "dchess · statistics", CP_ACC_ENGINE);
```

If `ww` is then reported unused, keep it: the rest of the function lays out columns with it.

- [ ] **Step 4: Build and test**

Run: `make 2>&1 | grep -cE 'error|warning'; make test 2>&1 | grep -E '^All |FAIL'`
Expected: `0`, then every suite passes.

- [ ] **Step 5: Look at them**

Run:
```bash
tmux kill-session -t v 2>/dev/null
tmux new-session -d -s v -x 100 -y 32 "./dchess"; sleep 2
echo "=== onboarding ==="; tmux capture-pane -p -t v | sed -n '6,24p'
tmux send-keys -t v Enter; sleep 1.5; tmux send-keys -t v Tab; sleep 1
echo "=== stats ==="; tmux capture-pane -p -t v | sed -n '6,26p'
tmux send-keys -t v q; sleep 0.5
tmux send-keys -t v "iloadfen 7k/8/8/8/8/8/8/K6B w - - 0 1" Enter; sleep 1.5
echo "=== game over ==="; tmux capture-pane -p -t v | sed -n '8,22p'
tmux kill-session -t v
```
Expected: all three drawn with rounded corners and a title inset into the top border, with their drop shadows intact.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(tui): restyle onboarding, game over and statistics

They move onto panel_frame() and the theme background, so no screen
still wears the old square-cornered look."
```

---

### Task 8: Verify across themes, terminals and sizes

Nothing new is built here. This task checks the result against the spec and fixes whatever the checks turn up, each fix as its own commit.

**Files:**
- Any, as fixes require.

- [ ] **Step 1: Every theme**

Run:
```bash
for t in gruvbox tokyonight btop catppuccin; do
  tmux kill-session -t v 2>/dev/null
  tmux new-session -d -s v -x 120 -y 40 "./dchess --no-menu -d easy --theme $t"
  sleep 2; for m in e2e4 d2d4; do tmux send-keys -t v "i$m" Enter; sleep 2.5; done
  echo "=== $t ==="; tmux capture-pane -e -p -t v | grep -o '48;5;[0-9]*' | sort -u | tr '\n' ' '; echo
  tmux kill-session -t v
done
```
Expected: each theme emits its own background indices (`48;5;235` for Gruvbox, `48;5;234` for Tokyo Night, `48;5;16` for btop, `48;5;235` for Catppuccin, among others).

- [ ] **Step 2: The two terminal classes**

Run the Task 5 capture loop once with `TERM=screen-256color` and once with `TERM=xterm` (8 colours) prefixed to the `./dchess` command.
Expected: both start without error. Under `xterm` the board and panels are drawn in the 8-colour fallback and stay legible.

- [ ] **Step 3: Two-player and a long game**

Run:
```bash
tmux kill-session -t v 2>/dev/null
tmux new-session -d -s v -x 120 -y 40 "./dchess --no-menu -2"; sleep 2
tmux capture-pane -p -t v | grep -o 'no engine'; tmux kill-session -t v
```
Expected: `no engine`.

Then drive a long game with the engine playing both sides. `go` plays whichever side is to move:

```bash
tmux kill-session -t v 2>/dev/null
tmux new-session -d -s v -x 120 -y 40 "./dchess --no-menu -d easy"; sleep 2
for i in $(seq 60); do tmux send-keys -t v "igo" Enter; sleep 1.8; done
tmux capture-pane -p -t v | cut -c86-120 | head -8; tmux kill-session -t v
```

Expected: the eval graph is filled across its full width. With 60 plies and a graph about 30 columns wide, it is showing only the newest evaluations.

- [ ] **Step 4: Resize**

Start at 120x40, then `tmux resize-window` to 70x22, 34x20 and back to 150x50, capturing each.
Expected: the side column appears, narrows and disappears at the documented widths, and nothing is drawn across a border at any size.

- [ ] **Step 5: Full suite**

Run: `make clean >/dev/null; make 2>&1 | grep -cE 'error|warning'; make test 2>&1 | grep -E '^All |FAIL'`
Expected: `0`, then every suite passes, perft included.
