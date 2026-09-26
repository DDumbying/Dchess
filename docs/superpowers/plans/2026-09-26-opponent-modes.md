# Opponent Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Either side of the board can be a human or the built-in engine at any level, including engine against engine with pausable auto-play, and the players can be changed mid-game.

**Architecture:** A per-side `Player` table and its pure rules live in `src/game/players.c`. The background search thread moves out of the TUI into an `Opponent` driver in `src/game/opponent.c`. Both are core code, so they are unit-tested. The TUI then keeps one driver per engine side and makes every "should an engine move now" decision in a single `drive_turn()`, called on each 100 ms tick.

**Tech Stack:** C (gcc -O2 -Wall), ncursesw, pthreads, plain `check()` test binaries run by `make test`.

**Spec:** `docs/superpowers/specs/2026-09-26-opponent-modes-design.md`

## Global Constraints

- The build must produce **zero warnings** under `gcc -O2 -Wall`.
- Tests link only `CORE_SRC` (`src/engine`, `src/game`, `src/utils`). `players.c` and `opponent.c` must not include ncurses or anything under `tui/`.
- Keep comments sparse, around 5–10% of lines, in the repo's style: short, and saying why, not what.
- Commits carry **no** `Co-Authored-By` trailer.
- The commit style is conventional: `feat:`, `fix:`, `docs:`, `test:`, with an optional scope such as `feat(tui):`.
- The auto-play delay between two engines is `PLAYERS_AUTOPLAY_DELAY_MS` = **500**.
- Names:
  - `player_label`: `You` for a human, `dchess Easy` / `dchess Medium` / `dchess Hard` for the engine.
  - PGN names: `Player` for a lone human, `Player 1` (White) and `Player 2` (Black) when both sides are human, `dchess (Hard)` for the engine.
  - Board title: two humans show as `Player 1 vs Player 2`.
- `make` only rebuilds `dchess` when a `.c` file changes. After header edits, build with `make -B dchess` and test with `make -B test`.

## Review Focus

Most likely first:

1. **Quitting while an engine thinks, in engine-vs-engine.** `q` must exit within a second, with no hang and no crash. The worker threads write into `TUIState`. Verified in Task 4, Step 9, scenario F.
2. **An engine-vs-engine game that ends.** The game-over popup appears, **no stats are recorded**, and `R` starts a new game that auto-plays again with the same players. Verified in Task 4, Step 9, scenario G.
3. **Changing a player while that side's engine is thinking.** For example, `black human` typed during Black's search. The search is cancelled and no engine move lands for the side that is now human. Verified in Task 4, Step 9, scenario D.
4. **A move by hand while an engine is to move and the game isn't paused.** A typed move or Enter on the board is refused with a message, and the move doesn't happen. Verified in Task 4, Step 9, scenario B.
5. **A terminal resize during auto-play.** There is no crash, and play continues once the terminal is big enough again. Verified in Task 6, Step 5.

---

### Task 1: Player model and rules

**Files:**
- Create: `headers/game/players.h`
- Create: `src/game/players.c`
- Test: `tests/test_players.c`

**Interfaces:**
- Consumes: `cli_depth_for_difficulty(int)`, `cli_time_limit_for_difficulty(int)`, `DIFF_EASY/DIFF_MEDIUM/DIFF_HARD` from `utils/cli.h`; `WHITE`, `BLACK` from `utils/constants.h`.
- Produces:
  ```c
  typedef enum { PLAYER_HUMAN, PLAYER_BUILTIN } PlayerKind;
  typedef struct { PlayerKind kind; int level; int depth; int time_ms; } Player;
  #define PLAYERS_AUTOPLAY_DELAY_MS 500
  Player      player_human(void);
  Player      player_builtin(int level);
  int         players_level_from_name(const char *name);   /* "easy"... -> DIFF_*, else -1 */
  const char *players_level_name(int level);                /* "Easy" / "Medium" / "Hard" */
  int  players_automated(const Player p[2], int side);
  int  players_undo_plies(const Player p[2], int side_to_move, int undo_count);
  int  players_should_start(const Player p[2], int side_to_move, int paused,
                            int game_over, long since_last_move_ms);
  int  players_stats_entry(const Player p[2], int *human_side, int *level);
  int  players_apply_command(Player p[2], const char *cmd, char *err, size_t n);
  void player_label(const Player *p, char *buf, size_t n);
  void players_pgn_name(const Player p[2], int side, char *buf, size_t n);
  void players_matchup(const Player p[2], char *buf, size_t n);   /* "You vs dchess Hard" */
  void players_describe(const Player p[2], char *buf, size_t n);  /* "White: You · Black: dchess Hard" */
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/test_players.c`:

```c
/* Player rules.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/players.h"
#include "utils/cli.h"
#include "utils/constants.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static Player H(void)       { return player_human(); }
static Player E(int level)  { return player_builtin(level); }

static void test_constructors(void)
{
    printf("== constructors ==\n");
    check("a human is PLAYER_HUMAN", H().kind == PLAYER_HUMAN);
    Player e = E(DIFF_HARD);
    check("a built-in keeps its level",
          e.kind == PLAYER_BUILTIN && e.level == DIFF_HARD);
    check("and takes that level's depth and time",
          e.depth == cli_depth_for_difficulty(DIFF_HARD) &&
          e.time_ms == cli_time_limit_for_difficulty(DIFF_HARD));
    check("an out-of-range level falls back to medium", E(7).level == DIFF_MEDIUM);
    check("level names parse",
          players_level_from_name("easy") == DIFF_EASY &&
          players_level_from_name("medium") == DIFF_MEDIUM &&
          players_level_from_name("hard") == DIFF_HARD);
    check("an unknown level name is -1", players_level_from_name("expert") == -1);
    check("level display names",
          strcmp(players_level_name(DIFF_EASY), "Easy") == 0 &&
          strcmp(players_level_name(DIFF_HARD), "Hard") == 0);
}

static void test_automated(void)
{
    printf("== automated ==\n");
    Player p[2] = { H(), E(DIFF_EASY) };
    check("a human side is not automated", !players_automated(p, WHITE));
    check("an engine side is", players_automated(p, BLACK));
}

static void test_undo_plies(void)
{
    printf("== undo ==\n");
    Player he[2] = { H(), E(DIFF_MEDIUM) };
    check("vs engine, human to move: 2 plies", players_undo_plies(he, WHITE, 10) == 2);
    check("vs engine, engine to move: 1 ply", players_undo_plies(he, BLACK, 10) == 1);
    check("vs engine, human to move, 1 ply of history: 0",
          players_undo_plies(he, WHITE, 1) == 0);

    Player eh[2] = { E(DIFF_MEDIUM), H() };
    check("engine as White, human to move: 2", players_undo_plies(eh, BLACK, 10) == 2);
    check("engine as White, engine to move: 1", players_undo_plies(eh, WHITE, 10) == 1);

    Player hh[2] = { H(), H() };
    check("two humans: 1 ply for either side",
          players_undo_plies(hh, WHITE, 5) == 1 && players_undo_plies(hh, BLACK, 5) == 1);

    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    check("two engines: 1 ply for either side",
          players_undo_plies(ee, WHITE, 5) == 1 && players_undo_plies(ee, BLACK, 5) == 1);

    check("no history: 0 in every pairing",
          players_undo_plies(he, BLACK, 0) == 0 &&
          players_undo_plies(hh, WHITE, 0) == 0 &&
          players_undo_plies(ee, WHITE, 0) == 0);
}

static void test_should_start(void)
{
    printf("== when an engine starts ==\n");
    Player he[2] = { H(), E(DIFF_MEDIUM) };
    check("an engine to move starts", players_should_start(he, BLACK, 0, 0, 100000));
    check("against a human there is no delay", players_should_start(he, BLACK, 0, 0, 0));
    check("a human to move never starts", !players_should_start(he, WHITE, 0, 0, 100000));
    check("not while paused", !players_should_start(he, BLACK, 1, 0, 100000));
    check("not once the game is over", !players_should_start(he, BLACK, 0, 1, 100000));

    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    check("two engines wait out the delay",
          !players_should_start(ee, WHITE, 0, 0, PLAYERS_AUTOPLAY_DELAY_MS - 1));
    check("and start once it has passed",
          players_should_start(ee, WHITE, 0, 0, PLAYERS_AUTOPLAY_DELAY_MS));

    Player hh[2] = { H(), H() };
    check("two humans: never",
          !players_should_start(hh, WHITE, 0, 0, 100000) &&
          !players_should_start(hh, BLACK, 0, 0, 100000));
}

static void test_stats_entry(void)
{
    printf("== which games are recorded ==\n");
    int side = -1, level = -1;

    Player he[2] = { H(), E(DIFF_HARD) };
    check("human vs engine is recorded", players_stats_entry(he, &side, &level) == 1);
    check("with the human's colour and the engine's level",
          side == WHITE && level == DIFF_HARD);

    Player eh[2] = { E(DIFF_EASY), H() };
    check("and with the human as Black",
          players_stats_entry(eh, &side, &level) == 1 && side == BLACK && level == DIFF_EASY);

    Player hh[2] = { H(), H() };
    check("two humans are not recorded", players_stats_entry(hh, &side, &level) == 0);
    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    check("two engines are not recorded", players_stats_entry(ee, &side, &level) == 0);
}

static void test_commands(void)
{
    printf("== player commands ==\n");
    char err[128];
    Player p[2] = { H(), E(DIFF_MEDIUM) };

    check("'black human' applies",
          players_apply_command(p, "black human", err, sizeof(err)) == 1 &&
          p[BLACK].kind == PLAYER_HUMAN);
    check("'white engine hard' applies",
          players_apply_command(p, "white engine hard", err, sizeof(err)) == 1 &&
          p[WHITE].kind == PLAYER_BUILTIN && p[WHITE].level == DIFF_HARD);
    check("'black engine' defaults to medium",
          players_apply_command(p, "black engine", err, sizeof(err)) == 1 &&
          p[BLACK].kind == PLAYER_BUILTIN && p[BLACK].level == DIFF_MEDIUM);

    players_apply_command(p, "white human", err, sizeof(err));
    check("'swap' exchanges the players",
          players_apply_command(p, "swap", err, sizeof(err)) == 1 &&
          p[WHITE].kind == PLAYER_BUILTIN && p[BLACK].kind == PLAYER_HUMAN);

    Player keep[2] = { p[WHITE], p[BLACK] };
    check("an unknown level is refused",
          players_apply_command(p, "white engine expert", err, sizeof(err)) == -1 &&
          strstr(err, "expert") != NULL);
    check("a level for a human is refused",
          players_apply_command(p, "black human hard", err, sizeof(err)) == -1);
    check("a bare colour is refused",
          players_apply_command(p, "white", err, sizeof(err)) == -1);
    check("an unknown role is refused",
          players_apply_command(p, "white robot", err, sizeof(err)) == -1);
    check("refused commands change nothing", memcmp(keep, p, sizeof(keep)) == 0);

    check("an unknown colour is not a player command",
          players_apply_command(p, "green human", err, sizeof(err)) == 0);
    check("a move is not a player command",
          players_apply_command(p, "b1c3", err, sizeof(err)) == 0);
    check("nor is 'new'", players_apply_command(p, "new", err, sizeof(err)) == 0);
}

static void test_labels(void)
{
    printf("== labels ==\n");
    char buf[64];
    Player h = H(), e = E(DIFF_HARD);

    player_label(&h, buf, sizeof(buf));
    check("a human is 'You'", strcmp(buf, "You") == 0);
    player_label(&e, buf, sizeof(buf));
    check("an engine is 'dchess Hard'", strcmp(buf, "dchess Hard") == 0);

    Player he[2] = { H(), E(DIFF_HARD) };
    players_pgn_name(he, WHITE, buf, sizeof(buf));
    check("PGN: a lone human is 'Player'", strcmp(buf, "Player") == 0);
    players_pgn_name(he, BLACK, buf, sizeof(buf));
    check("PGN: the engine is 'dchess (Hard)'", strcmp(buf, "dchess (Hard)") == 0);

    Player hh[2] = { H(), H() };
    players_pgn_name(hh, WHITE, buf, sizeof(buf));
    check("PGN: two humans are 'Player 1'", strcmp(buf, "Player 1") == 0);
    players_pgn_name(hh, BLACK, buf, sizeof(buf));
    check("and 'Player 2'", strcmp(buf, "Player 2") == 0);

    players_matchup(he, buf, sizeof(buf));
    check("matchup: 'You vs dchess Hard'", strcmp(buf, "You vs dchess Hard") == 0);
    players_matchup(hh, buf, sizeof(buf));
    check("matchup: 'Player 1 vs Player 2'", strcmp(buf, "Player 1 vs Player 2") == 0);
    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    players_matchup(ee, buf, sizeof(buf));
    check("matchup: 'dchess Easy vs dchess Hard'",
          strcmp(buf, "dchess Easy vs dchess Hard") == 0);

    players_describe(he, buf, sizeof(buf));
    check("describe: 'White: You · Black: dchess Hard'",
          strcmp(buf, "White: You · Black: dchess Hard") == 0);
}

int main(void)
{
    test_constructors();
    test_automated();
    test_undo_plies();
    test_should_start();
    test_stats_entry();
    test_commands();
    test_labels();

    if (failures) {
        printf("\n%d player test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll player tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_players`
Expected: FAIL. The compile error is `game/players.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `headers/game/players.h`:

```c
#ifndef PLAYERS_H
#define PLAYERS_H

#include <stddef.h>

typedef enum { PLAYER_HUMAN, PLAYER_BUILTIN } PlayerKind;

/* level, depth and time_ms mean nothing for a human. */
typedef struct {
    PlayerKind kind;
    int        level;     /* DIFF_EASY / DIFF_MEDIUM / DIFF_HARD */
    int        depth;
    int        time_ms;
} Player;

/* Between two engines, so a person can follow the game. */
#define PLAYERS_AUTOPLAY_DELAY_MS 500

Player      player_human(void);
Player      player_builtin(int level);
int         players_level_from_name(const char *name);
const char *players_level_name(int level);

int  players_automated(const Player p[2], int side);

/* Plies "undo" takes back, or 0 when there is not enough history. */
int  players_undo_plies(const Player p[2], int side_to_move, int undo_count);

int  players_should_start(const Player p[2], int side_to_move, int paused,
                          int game_over, long since_last_move_ms);

/* 1 when exactly one side is human and the other is the built-in engine. */
int  players_stats_entry(const Player p[2], int *human_side, int *level);

/* 1 applied, 0 not a player command, -1 invalid with a message in err. */
int  players_apply_command(Player p[2], const char *cmd, char *err, size_t n);

void player_label(const Player *p, char *buf, size_t n);
void players_pgn_name(const Player p[2], int side, char *buf, size_t n);
void players_matchup(const Player p[2], char *buf, size_t n);
void players_describe(const Player p[2], char *buf, size_t n);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/game/players.c`:

```c
#include "game/players.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include <stdio.h>
#include <string.h>

Player player_human(void)
{
    Player p = { PLAYER_HUMAN, DIFF_MEDIUM, 0, 0 };
    return p;
}

Player player_builtin(int level)
{
    if (level < DIFF_EASY || level > DIFF_HARD) level = DIFF_MEDIUM;
    Player p = { PLAYER_BUILTIN, level,
                 cli_depth_for_difficulty(level),
                 cli_time_limit_for_difficulty(level) };
    return p;
}

int players_level_from_name(const char *name)
{
    if (strcmp(name, "easy") == 0)   return DIFF_EASY;
    if (strcmp(name, "medium") == 0) return DIFF_MEDIUM;
    if (strcmp(name, "hard") == 0)   return DIFF_HARD;
    return -1;
}

const char *players_level_name(int level)
{
    return level == DIFF_EASY ? "Easy" : level == DIFF_HARD ? "Hard" : "Medium";
}

static int humans(const Player p[2])
{
    return (p[WHITE].kind == PLAYER_HUMAN) + (p[BLACK].kind == PLAYER_HUMAN);
}

int players_automated(const Player p[2], int side)
{
    return p[side].kind != PLAYER_HUMAN;
}

int players_undo_plies(const Player p[2], int side_to_move, int undo_count)
{
    /* Against an engine, undo hands the turn back to the human. */
    int n = (humans(p) == 1 && !players_automated(p, side_to_move)) ? 2 : 1;
    return undo_count >= n ? n : 0;
}

int players_should_start(const Player p[2], int side_to_move, int paused,
                         int game_over, long since_last_move_ms)
{
    if (game_over || paused || !players_automated(p, side_to_move)) return 0;
    if (humans(p) == 0 && since_last_move_ms < PLAYERS_AUTOPLAY_DELAY_MS) return 0;
    return 1;
}

int players_stats_entry(const Player p[2], int *human_side, int *level)
{
    if (humans(p) != 1) return 0;
    int h = (p[WHITE].kind == PLAYER_HUMAN) ? WHITE : BLACK;
    int e = (h == WHITE) ? BLACK : WHITE;
    if (p[e].kind != PLAYER_BUILTIN) return 0;
    if (human_side) *human_side = h;
    if (level)      *level      = p[e].level;
    return 1;
}

int players_apply_command(Player p[2], const char *cmd, char *err, size_t n)
{
    if (strcmp(cmd, "swap") == 0) {
        Player t = p[WHITE];
        p[WHITE] = p[BLACK];
        p[BLACK] = t;
        return 1;
    }

    char colour[8], who[8], level[16], extra[2];
    int got = sscanf(cmd, "%7s %7s %15s %1s", colour, who, level, extra);
    if (got < 1) return 0;

    int side = strcmp(colour, "white") == 0 ? WHITE :
               strcmp(colour, "black") == 0 ? BLACK : -1;
    if (side < 0) return 0;

    const char *usage =
        "Use: white|black human, or white|black engine [easy|medium|hard]";

    if (got >= 2 && strcmp(who, "human") == 0) {
        if (got > 2) {
            snprintf(err, n, "A human has no level. %s", usage);
            return -1;
        }
        p[side] = player_human();
        return 1;
    }
    if (got >= 2 && got <= 3 && strcmp(who, "engine") == 0) {
        int lv = DIFF_MEDIUM;
        if (got == 3 && (lv = players_level_from_name(level)) < 0) {
            snprintf(err, n, "Unknown level '%s'. Use: easy | medium | hard", level);
            return -1;
        }
        p[side] = player_builtin(lv);
        return 1;
    }
    snprintf(err, n, "%s", usage);
    return -1;
}

void player_label(const Player *p, char *buf, size_t n)
{
    if (p->kind == PLAYER_HUMAN)
        snprintf(buf, n, "You");
    else
        snprintf(buf, n, "dchess %s", players_level_name(p->level));
}

/* Two humans are told apart by number rather than both being "You". */
static void side_name(const Player p[2], int side, char *buf, size_t n)
{
    if (humans(p) == 2)
        snprintf(buf, n, "Player %d", side == WHITE ? 1 : 2);
    else
        player_label(&p[side], buf, n);
}

void players_pgn_name(const Player p[2], int side, char *buf, size_t n)
{
    if (p[side].kind == PLAYER_BUILTIN)
        snprintf(buf, n, "dchess (%s)", players_level_name(p[side].level));
    else if (humans(p) == 2)
        snprintf(buf, n, "Player %d", side == WHITE ? 1 : 2);
    else
        snprintf(buf, n, "Player");
}

void players_matchup(const Player p[2], char *buf, size_t n)
{
    char w[32], b[32];
    side_name(p, WHITE, w, sizeof(w));
    side_name(p, BLACK, b, sizeof(b));
    snprintf(buf, n, "%s vs %s", w, b);
}

void players_describe(const Player p[2], char *buf, size_t n)
{
    char w[32], b[32];
    side_name(p, WHITE, w, sizeof(w));
    side_name(p, BLACK, b, sizeof(b));
    snprintf(buf, n, "White: %s · Black: %s", w, b);
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/test_players 2>&1 | grep -c warning; ./build/test_players`
Expected: `0` warnings, then `All player tests passed.`

- [ ] **Step 6: Run the whole suite**

Run: `make -B test 2>&1 | tail -3`
Expected: `All suites passed.`

- [ ] **Step 7: Commit**

```bash
git add headers/game/players.h src/game/players.c tests/test_players.c
git commit -m "feat: per-side player model and rules"
```

---

### Task 2: Opponent driver for the built-in engine

**Files:**
- Create: `headers/game/opponent.h`
- Create: `src/game/opponent.c`
- Test: `tests/test_opponent.c`

**Interfaces:**
- Consumes: `search()`, `search_cancel()`, `SearchResult` from `engine/search.h`; `Position` from `engine/board.h`; `U64` from `utils/types.h`.
- Produces:
  ```c
  typedef struct Opponent Opponent;
  Opponent *opponent_builtin(int depth, int time_ms);   /* NULL if out of memory */
  int  opponent_start (Opponent *o, const Position *pos, U64 key);   /* 0 while busy */
  int  opponent_poll  (Opponent *o, SearchResult *out, U64 *key);    /* 1 once, per search */
  void opponent_stop  (Opponent *o);   /* finish now; next poll returns best so far */
  void opponent_cancel(Opponent *o);   /* finish now and discard */
  void opponent_free  (Opponent *o);   /* NULL-safe; cancels first */
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/test_opponent.c`:

```c
/* The built-in engine driver.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <time.h>
#include "game/opponent.h"
#include "engine/fen.h"
#include "engine/move.h"
#include "utils/bitboard.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void nap(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

static int wait_result(Opponent *o, SearchResult *r, U64 *key, long budget_ms)
{
    long t0 = now_ms();
    while (now_ms() - t0 < budget_ms) {
        if (opponent_poll(o, r, key)) return 1;
        nap(5);
    }
    return 0;
}

static const char *START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static void test_finds_mate(void)
{
    printf("== a search from start to result ==\n");
    Position pos;
    parse_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", &pos, NULL, NULL);

    SearchResult r;
    U64 key = 0;
    Opponent *o = opponent_builtin(3, 5000);
    check("the driver is created", o != NULL);
    check("poll before any start returns 0", !opponent_poll(o, &r, &key));
    check("start is accepted", opponent_start(o, &pos, 0x1234));
    check("a second start while busy is refused", !opponent_start(o, &pos, 0x9999));
    check("a result arrives", wait_result(o, &r, &key, 10000));
    check("it is the back-rank mate Ra1-a8", FROM(r.best_move) == 0 && TO(r.best_move) == 56);
    check("the key comes back with it", key == 0x1234);
    check("the driver is idle again", !opponent_poll(o, &r, &key));
    opponent_free(o);
}

static void test_cancel(void)
{
    printf("== cancel ==\n");
    Position pos;
    parse_fen(START, &pos, NULL, NULL);

    SearchResult r;
    U64 key;
    Opponent *o = opponent_builtin(20, 20000);
    opponent_start(o, &pos, 1);
    long t0 = now_ms();
    opponent_cancel(o);
    check("cancel straight after start returns within 1s", now_ms() - t0 < 1000);
    check("and leaves no result to poll", !opponent_poll(o, &r, &key));
    check("the driver can start again", opponent_start(o, &pos, 2));
    opponent_cancel(o);
    opponent_free(o);
    opponent_free(NULL);
    check("freeing NULL is harmless", 1);
}

static void test_stop(void)
{
    printf("== stop ==\n");
    Position pos;
    parse_fen(START, &pos, NULL, NULL);

    SearchResult r;
    U64 key = 0;
    Opponent *o = opponent_builtin(20, 20000);
    opponent_start(o, &pos, 3);
    nap(200);
    long t0 = now_ms();
    opponent_stop(o);
    check("stop returns within 1s", now_ms() - t0 < 1000);
    check("the next poll has a result", opponent_poll(o, &r, &key));
    check("with the key and a move", key == 3 && r.best_move != 0);
    opponent_free(o);
}

int main(void)
{
    init_attacks();

    test_finds_mate();
    test_cancel();
    test_stop();

    if (failures) {
        printf("\n%d opponent test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll opponent tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_opponent`
Expected: FAIL. The compile error is `game/opponent.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `headers/game/opponent.h`:

```c
#ifndef OPPONENT_H
#define OPPONENT_H

#include "engine/board.h"
#include "engine/search.h"
#include "utils/types.h"

/* An engine playing one side. Searches run in the background: start one,
 * then poll from the main loop until its result comes back. */
typedef struct Opponent Opponent;

Opponent *opponent_builtin(int depth, int time_ms);

int  opponent_start (Opponent *o, const Position *pos, U64 key);
int  opponent_poll  (Opponent *o, SearchResult *out, U64 *key);
void opponent_stop  (Opponent *o);
void opponent_cancel(Opponent *o);
void opponent_free  (Opponent *o);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/game/opponent.c`:

```c
#include "game/opponent.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

/* busy and threaded belong to the owning thread. ready and result are
 * the worker's handoff and are guarded by mutex. */
struct Opponent {
    int             depth, time_ms;
    int             busy;
    int             threaded;
    pthread_t       thread;
    pthread_mutex_t mutex;
    int             ready;
    SearchResult    result;
    Position        snapshot;   /* the worker's private copy */
    U64             key;
};

Opponent *opponent_builtin(int depth, int time_ms)
{
    Opponent *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->depth   = depth;
    o->time_ms = time_ms;
    pthread_mutex_init(&o->mutex, NULL);
    return o;
}

static void *worker(void *arg)
{
    Opponent *o = arg;
    SearchResult r = search(&o->snapshot, o->depth, o->time_ms);

    pthread_mutex_lock(&o->mutex);
    o->result = r;
    o->ready  = 1;
    pthread_mutex_unlock(&o->mutex);
    return NULL;
}

static int is_ready(Opponent *o)
{
    pthread_mutex_lock(&o->mutex);
    int r = o->ready;
    pthread_mutex_unlock(&o->mutex);
    return r;
}

int opponent_start(Opponent *o, const Position *pos, U64 key)
{
    if (o->busy) return 0;
    o->snapshot = *pos;
    o->key      = key;
    o->ready    = 0;
    o->busy     = 1;
    o->threaded = (pthread_create(&o->thread, NULL, worker, o) == 0);
    if (!o->threaded) worker(o);   /* no thread to be had: search here instead */
    return 1;
}

static void finish(Opponent *o)
{
    if (!o->threaded) return;
    /* search() clears any earlier cancel as it begins, so one sent before
     * the worker got that far would be lost. Keep asking until it answers. */
    while (!is_ready(o)) {
        search_cancel();
        struct timespec ms = { 0, 1000000L };
        nanosleep(&ms, NULL);
    }
    pthread_join(o->thread, NULL);
    o->threaded = 0;
}

int opponent_poll(Opponent *o, SearchResult *out, U64 *key)
{
    if (!o->busy || !is_ready(o)) return 0;
    if (o->threaded) {
        pthread_join(o->thread, NULL);
        o->threaded = 0;
    }
    if (out) *out = o->result;
    if (key) *key = o->key;
    o->busy = 0;
    return 1;
}

void opponent_stop(Opponent *o)
{
    if (o->busy) finish(o);
}

void opponent_cancel(Opponent *o)
{
    if (!o->busy) return;
    finish(o);
    o->busy  = 0;
    o->ready = 0;
}

void opponent_free(Opponent *o)
{
    if (!o) return;
    opponent_cancel(o);
    pthread_mutex_destroy(&o->mutex);
    free(o);
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/test_opponent 2>&1 | grep -c warning; ./build/test_opponent`
Expected: `0` warnings, then `All opponent tests passed.`

- [ ] **Step 6: Run the whole suite**

Run: `make -B test 2>&1 | tail -3`
Expected: `All suites passed.`

- [ ] **Step 7: Commit**

```bash
git add headers/game/opponent.h src/game/opponent.c tests/test_opponent.c
git commit -m "feat: background driver for the built-in engine"
```

---

### Task 3: `--white` and `--black` on the command line

The TUI still runs on the old fields after this task, through a short bridge in `tui_init()` and the onboarding hand-off. Task 4 replaces both.

**Files:**
- Modify: `headers/utils/cli.h` (the `CliArgs` struct)
- Modify: `src/utils/cli.c` (`cli_parse`, `cli_help`)
- Modify: `src/tui/tui.c` (`tui_init`, around lines 278–299)
- Modify: `src/tui/onboard.c` (the `chosen` block at the end of `tui_onboarding`)
- Modify: `README.md` (the CLI block)
- Test: `tests/test_cli.c`

**Interfaces:**
- Consumes: `Player`, `player_human()`, `player_builtin(int)`, `players_level_from_name(const char *)` from Task 1.
- Produces: `CliArgs.players[2]` (a `Player` for each of `WHITE` and `BLACK`), which replaces `player_side`, `difficulty`, `engine_depth` and `two_player`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_cli.c`:

```c
/* Command-line parsing.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "utils/cli.h"
#include "utils/constants.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Splits on spaces; argv[0] is the program name. */
static int parse(CliArgs *a, const char *line)
{
    static char buf[256];
    char *argv[32];
    int argc = 0;
    snprintf(buf, sizeof(buf), "%s", line);
    argv[argc++] = "dchess";
    for (char *t = strtok(buf, " "); t && argc < 32; t = strtok(NULL, " "))
        argv[argc++] = t;
    return cli_parse(argc, argv, a);
}

static int human(const Player *p)             { return p->kind == PLAYER_HUMAN; }
static int engine(const Player *p, int level) { return p->kind == PLAYER_BUILTIN && p->level == level; }

int main(void)
{
    CliArgs a;

    printf("== players ==\n");
    check("no flags parses", parse(&a, "") == 0);
    check("default: you as White against dchess Medium",
          human(&a.players[WHITE]) && engine(&a.players[BLACK], DIFF_MEDIUM));
    check("the default engine has medium's depth",
          a.players[BLACK].depth == cli_depth_for_difficulty(DIFF_MEDIUM));

    parse(&a, "-c black -d hard");
    check("-c black -d hard: engine White at hard, you Black",
          engine(&a.players[WHITE], DIFF_HARD) && human(&a.players[BLACK]));

    parse(&a, "-2");
    check("-2: two humans", human(&a.players[WHITE]) && human(&a.players[BLACK]));

    check("--white hard --black easy parses",
          parse(&a, "--white hard --black easy") == 0);
    check("and is engine against engine",
          engine(&a.players[WHITE], DIFF_HARD) && engine(&a.players[BLACK], DIFF_EASY));
    check("and skips the menu like any gameplay flag", a.any_gameplay_flag);

    parse(&a, "--black hard -2");
    check("--black overrides a later -2",
          human(&a.players[WHITE]) && engine(&a.players[BLACK], DIFF_HARD));
    parse(&a, "-2 --black hard");
    check("and an earlier one",
          human(&a.players[WHITE]) && engine(&a.players[BLACK], DIFF_HARD));
    parse(&a, "--white human -c black");
    check("--white overrides -c", human(&a.players[WHITE]) && human(&a.players[BLACK]));

    printf("== errors ==\n");
    check("a bad value is an error",
          parse(&a, "--white wizard") != 0 && a.error && strstr(a.error_msg, "wizard"));
    check("a missing value is an error", parse(&a, "--black") != 0 && a.error);

    if (failures) {
        printf("\n%d CLI test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll CLI tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_cli`
Expected: FAIL. The compile error is `'CliArgs' has no member named 'players'`.

- [ ] **Step 3: Change `CliArgs`**

In `headers/utils/cli.h`, add `#include "game/players.h"` after `#define CLI_H`. Then replace these four fields:

```c
    int player_side;    /* WHITE(0) or BLACK(1), default WHITE */
    int difficulty;     /* DIFF_EASY / DIFF_MEDIUM / DIFF_HARD */
    int engine_depth;   /* derived from difficulty */
```
and
```c
    int two_player;     /* --two-player flag: no engine, board flips */
```
with this single field, placed where `player_side` was:

```c
    Player players[2];  /* by colour; from -c/-d/-2, then --white/--black */
```

Update the `any_gameplay_flag` comment to read `/* any of -c -d -2 --white --black --fen --theme */`.

- [ ] **Step 4: Parse the flags**

In `src/utils/cli.c`, `cli_parse`:

1. Replace the four default lines for `player_side`, `difficulty`, `engine_depth` and `two_player` with:

```c
    args->players[WHITE] = player_human();
    args->players[BLACK] = player_builtin(DIFF_MEDIUM);

    /* -c, -d and -2 shape the game; --white and --black then override
     * their side whatever the order they came in. */
    int colour = WHITE, level = DIFF_MEDIUM, two = 0;
    int chosen_set[2] = { 0, 0 };
    Player chosen[2];
```

2. In the `--color` branch, change `args->player_side = WHITE;` to `colour = WHITE;` and `args->player_side = BLACK;` to `colour = BLACK;`.

3. In the `--difficulty` branch, change the three `args->difficulty   = DIFF_…;` lines to `level = DIFF_…;`, and delete `args->engine_depth = diff_to_depth[args->difficulty];`.

4. In the `--two-player` branch, change `args->two_player = 1;` to `two = 1;`.

5. Insert this branch just before `/* --fen <string>`:

```c
        /* --white / --black ────────────────────────────────────────── */
        if (strcmp(a, "--white") == 0 || strcmp(a, "--black") == 0) {
            int side = (a[2] == 'w') ? WHITE : BLACK;
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '%s' requires an argument: human|easy|medium|hard", a);
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            int lv = players_level_from_name(val);
            if (strcmp(val, "human") == 0) {
                chosen[side] = player_human();
            } else if (lv >= 0) {
                chosen[side] = player_builtin(lv);
            } else {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Unknown player '%s'. Use: human | easy | medium | hard", val);
                args->error = 1;
                return -1;
            }
            chosen_set[side] = 1;
            args->any_gameplay_flag = 1;
            continue;
        }
```

6. Replace the final `return 0;` of `cli_parse` with:

```c
    if (two) {
        args->players[WHITE] = args->players[BLACK] = player_human();
    } else {
        args->players[colour]         = player_human();
        args->players[colour ^ BLACK] = player_builtin(level);
    }
    for (int s = WHITE; s <= BLACK; s++)
        if (chosen_set[s]) args->players[s] = chosen[s];

    return 0;
```

7. `diff_to_depth` is still used by `cli_depth_for_difficulty`, so keep it.

8. In `cli_help`, replace the line `"    -2, --two-player\n"` with:

```c
        "    -2, --two-player\n"
        "          Two people at one keyboard; no engine.\n"
        "\n"
        "    --white <human|easy|medium|hard>\n"
        "    --black <human|easy|medium|hard>\n"
        "          Choose who plays one side. Overrides -c, -d and -2.\n"
        "          Give both an engine level to watch it play itself:\n"
        "            dchess --white hard --black easy\n"
```

- [ ] **Step 5: Bridge the TUI to the new fields**

In `src/tui/tui.c`, `tui_init`, replace:

```c
    state->player_side  = args ? args->player_side  : WHITE;
    state->difficulty   = args ? args->difficulty   : DIFF_MEDIUM;
    state->engine_depth = args ? args->engine_depth : 5;
    state->two_player   = args ? args->two_player   : 0;
```
with:
```c
    const Player *pl = args ? args->players : NULL;
    int w_human = !pl || pl[WHITE].kind == PLAYER_HUMAN;
    int b_human =  pl && pl[BLACK].kind == PLAYER_HUMAN;
    state->two_player   = w_human && b_human;
    state->player_side  = (!w_human && b_human) ? BLACK : WHITE;
    state->difficulty   = pl ? pl[state->player_side == WHITE ? BLACK : WHITE].level
                             : DIFF_MEDIUM;
    state->engine_depth = cli_depth_for_difficulty(state->difficulty);
```

In `src/tui/onboard.c`, at the end of `tui_onboarding`, replace:

```c
    chosen.player_side  = (choice.side == BLACK) ? BLACK : WHITE;
    chosen.two_player   = (choice.side == SIDE_TWO_PLAYER);
    chosen.difficulty   = choice.difficulty;
    chosen.engine_depth = cli_depth_for_difficulty(choice.difficulty);
```
with:
```c
    if (choice.side == SIDE_TWO_PLAYER) {
        chosen.players[WHITE] = chosen.players[BLACK] = player_human();
    } else {
        chosen.players[choice.side]         = player_human();
        chosen.players[choice.side ^ BLACK] = player_builtin(choice.difficulty);
    }
```

- [ ] **Step 6: Update the README's CLI block**

In `README.md`, after the line `  -2, --two-player                Local two-player mode — no engine, board flips after each move`, add:

```
  --white <human|easy|medium|hard>
  --black <human|easy|medium|hard>
                                  Choose who plays a side; overrides -c, -d and -2
```

After the line `  dchess --two-player             Local two-player, board flips each turn`, add:

```
  dchess --white hard --black easy
                                  Watch the engine play itself
```

- [ ] **Step 7: Run the tests and the build**

Run: `make -B test 2>&1 | tail -3; ./build/test_cli | tail -1; make -B dchess 2>&1 | grep -c warning`
Expected: `All suites passed.`, then `All CLI tests passed.`, then `0`.

- [ ] **Step 8: Smoke-check that existing behaviour is unchanged**

Run:
```bash
tmux kill-session -t op 2>/dev/null
tmux new-session -d -s op -x 120 -y 40 "./dchess -c black -d easy"
sleep 3; tmux capture-pane -p -t op | head -30
tmux kill-session -t op
```
Expected: the engine has already played White's first move (the moves panel shows `1.` with a move), and the status line mentions Black and Easy.

- [ ] **Step 9: Commit**

```bash
git add headers/utils/cli.h src/utils/cli.c src/tui/tui.c src/tui/onboard.c README.md tests/test_cli.c
git commit -m "feat(cli): choose each side's player with --white and --black"
```

---

### Task 4: Drive the game from the player table

This task replaces `engine_side`, `two_player`, `player_side`, `difficulty`, `engine_depth`, `time_limit_ms` and the `search_*` thread fields with the player table and drivers. It also adds pause, resume, the player commands and the new `flip`.

**Files:**
- Modify: `headers/tui/tui.h`
- Replace: `src/tui/commands.c`
- Modify: `headers/tui/commands.h`
- Modify: `src/tui/tui.c`
- Modify: `src/tui/onboard.c` (only the `choice` initialisation)
- Modify: `src/tui/panels.c` (`draw_engine_panel`, the first two conditions)
- Modify: `src/tui/input.c` (let Space through in normal mode)
- Modify: `src/utils/cli.c` (`cli_help` in-game commands)
- Modify: `README.md` (command list and features)

**Interfaces:**
- Consumes: everything Task 1 produces; `opponent_*` from Task 2; `CliArgs.players[2]` from Task 3.
- Produces (used by Tasks 5 and 6):
  - `TUIState` fields:
    - `Player players[2]`
    - `Opponent *drivers[2]`
    - `Opponent *go_driver`
    - `Opponent *thinking` (NULL when idle)
    - `char thinking_by[32]`
    - `char last_search_by[32]`
    - `int paused`
    - `long last_move_ms`
  - In `commands.h`:
    ```c
    int  drive_turn(TUIState *state);
    void cancel_engine_search(TUIState *state);
    void tui_attach_players(TUIState *state);
    void tui_release_players(TUIState *state);
    int  tui_can_move_by_hand(const TUIState *state);
    void tui_new_game(TUIState *state);
    void tui_undo(TUIState *state);
    void describe_setup(const TUIState *state, char *buf, size_t n);
    ```
    `poll_engine_search` and `difficulty_label` are removed.

- [ ] **Step 1: Confirm who uses the fields being removed**

Run: `grep -rn "engine_side\|two_player\|player_side\|->difficulty\|engine_depth\|time_limit_ms\|search_running\|search_ready\|search_mutex\|search_thread\|search_snapshot\|poll_engine_search\|difficulty_label" src headers --include=*.c --include=*.h | grep -v "^src/utils/cli.c\|^headers/utils/cli.h\|^src/engine"`
Expected: matches only in `headers/tui/tui.h`, `headers/tui/commands.h`, `src/tui/commands.c`, `src/tui/tui.c`, `src/tui/onboard.c` and `src/tui/panels.c`. Any other file must be added to this task's edits.

- [ ] **Step 2: Rewrite the state struct**

In `headers/tui/tui.h`:

1. Replace `#include <pthread.h>` with:

```c
#include "game/players.h"
#include "game/opponent.h"
```

2. Delete these fields and their comments: `engine_depth`, `engine_side`, `player_side`, `difficulty`, `two_player`, `time_limit_ms`, and the whole background-search block from `/* Background engine search.` through `U64 search_snapshot_hash;`.

3. After `SearchResult last_search; /* nodes == 0 until the first search */`, add:

```c
    char     last_search_by[32];   /* the engine behind last_search */
```

4. Before `/* Persistent statistics */`, add:

```c
    /* Who plays each side. Drivers are built by tui_attach_players(), not
     * tui_init(), which runs twice and would leak the first set. */
    Player    players[2];
    Opponent *drivers[2];     /* NULL for a human */
    Opponent *go_driver;      /* "go" on a human's turn */
    Opponent *thinking;       /* the driver searching now, or NULL */
    char      thinking_by[32];
    int       paused;
    long      last_move_ms;   /* monotonic; when an engine last moved */
```

5. Change the `view_side` comment to `/* Side the board is drawn from; turns each move between two humans. */`.

- [ ] **Step 3: Replace `commands.h`**

Replace the body of `headers/tui/commands.h`, between the include guard and `#endif`, with:

```c
#include "tui/tui.h"
#include <stddef.h>

/* Returns -1 on quit, 1 otherwise. */
int handle_command(TUIState *state, const char *cmd);

/* Call once per main-loop tick. Applies a finished search, or starts the
 * side to move's engine when it is due. Returns 1 when a search came
 * back, in which case the caller should call game_update_status(). */
int drive_turn(TUIState *state);

/* Discards whatever is being searched. No-op when nothing is. */
void cancel_engine_search(TUIState *state);

/* Builds a driver for every engine side. Call once the players are final;
 * tui_release_players() frees them. */
void tui_attach_players(TUIState *state);
void tui_release_players(TUIState *state);

/* Whether the person at the keyboard may move for the side to move. */
int tui_can_move_by_hand(const TUIState *state);

/* Keeps the players and clears any pause. Used by "new" and the game-over
 * popup. */
void tui_new_game(TUIState *state);

/* Takes back players_undo_plies() plies and starts nothing; pauses when
 * both sides are engines, which would otherwise replay the move. */
void tui_undo(TUIState *state);

/* "White: You · Black: dchess Medium" */
void describe_setup(const TUIState *state, char *buf, size_t n);
```

- [ ] **Step 4: Replace `commands.c`**

Replace the whole of `src/tui/commands.c` with:

```c
#include "tui/commands.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/search.h"
#include "engine/move.h"
#include "engine/hash.h"
#include "engine/fen.h"
#include "game/pgn.h"
#include "tui/render.h"
#include "utils/theme.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

void describe_setup(const TUIState *state, char *buf, size_t n)
{
    players_describe(state->players, buf, n);
}

void cancel_engine_search(TUIState *state)
{
    if (!state->thinking) return;
    opponent_cancel(state->thinking);
    state->thinking = NULL;
}

static void attach(TUIState *state, int side)
{
    if (state->drivers[side] && state->thinking == state->drivers[side])
        cancel_engine_search(state);
    opponent_free(state->drivers[side]);
    state->drivers[side] = NULL;

    const Player *p = &state->players[side];
    if (p->kind == PLAYER_BUILTIN)
        state->drivers[side] = opponent_builtin(p->depth, p->time_ms);
}

void tui_attach_players(TUIState *state)
{
    attach(state, WHITE);
    attach(state, BLACK);
    if (!state->go_driver) {
        Player m = player_builtin(DIFF_MEDIUM);
        state->go_driver = opponent_builtin(m.depth, m.time_ms);
    }
}

void tui_release_players(TUIState *state)
{
    cancel_engine_search(state);
    for (int side = WHITE; side <= BLACK; side++) {
        opponent_free(state->drivers[side]);
        state->drivers[side] = NULL;
    }
    opponent_free(state->go_driver);
    state->go_driver = NULL;
}

int tui_can_move_by_hand(const TUIState *state)
{
    return state->paused || !players_automated(state->players, state->game.pos.side);
}

static int both_engines(const TUIState *state)
{
    return players_automated(state->players, WHITE) &&
           players_automated(state->players, BLACK);
}

static int same_player(const Player *a, const Player *b)
{
    return a->kind == b->kind && a->level == b->level &&
           a->depth == b->depth && a->time_ms == b->time_ms;
}

void tui_undo(TUIState *state)
{
    cancel_engine_search(state);

    int n = players_undo_plies(state->players, state->game.pos.side,
                               state->game.undo_count);
    if (!n) {
        snprintf(state->status, sizeof(state->status), "Nothing to undo");
        return;
    }
    for (int i = 0; i < n; i++)
        game_undo(&state->game);

    /* Otherwise the engine would play the move straight back. */
    if (both_engines(state)) state->paused = 1;

    int cp;
    if (game_last_eval(&state->game, &cp))
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", cp / 100.0f);
    else
        snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");

    state->selected = 0;
    memset(state->highlight, 0, sizeof(state->highlight));
    snprintf(state->status, sizeof(state->status), "Took back %d %s%s",
             n, n == 1 ? "move" : "moves", state->paused ? " — paused" : "");
}

void tui_new_game(TUIState *state)
{
    cancel_engine_search(state);

    game_reset(&state->game);
    memset(&state->last_search, 0, sizeof(state->last_search));
    state->last_search_by[0] = '\0';
    state->paused = 0;

    state->selected  = 0;
    state->view_side = WHITE;
    memset(state->highlight, 0, sizeof(state->highlight));
    snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");

    char setup[128];
    describe_setup(state, setup, sizeof(setup));
    snprintf(state->status, sizeof(state->status), "New game – %s", setup);
}

static int try_move(TUIState *state, const char *movestr)
{
    int from, to, promo;
    if (!parse_move_str(movestr, &from, &to, &promo)) {
        snprintf(state->status, sizeof(state->status), "Bad move format: %s", movestr);
        return 0;
    }

    Move m;
    if (!game_find_move(&state->game, from, to, promo, &m)) {
        snprintf(state->status, sizeof(state->status), "Illegal move: %s", movestr);
        return 0;
    }

    game_play(&state->game, m);
    snprintf(state->status, sizeof(state->status), "Played: %s",
             state->game.move_history[state->game.move_count - 1]);
    return 1;
}

static void apply_engine_result(TUIState *state, SearchResult res, const char *by)
{
    if (!res.best_move) {
        /* A stopped search also comes back empty (see search.h), so ask
         * the board whether the game is really over. */
        if (has_legal_moves(&state->game.pos)) {
            /* Or the next tick would start the same search again. */
            state->paused = 1;
            snprintf(state->status, sizeof(state->status),
                     "Search stopped before it found a move — paused");
            return;
        }
        game_update_status(&state->game);
        snprintf(state->status, sizeof(state->status), "%s", state->game.result);
        return;
    }

    int score_white = eval_white_view(res.best_score, state->game.pos.side);
    float eval_f = score_white / 100.0f;
    snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", eval_f);
    game_record_eval(&state->game, score_white);
    state->last_search = res;
    snprintf(state->last_search_by, sizeof(state->last_search_by), "%s", by);

    game_play(&state->game, res.best_move);
    state->last_move_ms = now_ms();

    snprintf(state->status, sizeof(state->status), "%s: %s (eval %+.2f, depth %d)",
             by, state->game.move_history[state->game.move_count - 1],
             eval_f, res.depth_reached);
}

static void start_thinking(TUIState *state, Opponent *o, const char *by)
{
    if (!o) {
        snprintf(state->status, sizeof(state->status), "Could not start %s", by);
        return;
    }
    if (!opponent_start(o, &state->game.pos, game_hash(&state->game))) return;

    state->thinking = o;
    snprintf(state->thinking_by, sizeof(state->thinking_by), "%s", by);
    snprintf(state->status, sizeof(state->status), "%s thinking...", by);
    if (state->request_redraw) state->request_redraw(state->redraw_ctx);
}

int drive_turn(TUIState *state)
{
    if (state->thinking) {
        SearchResult res;
        U64 key;
        if (!opponent_poll(state->thinking, &res, &key)) return 0;
        state->thinking = NULL;

        /* A result for a board that has since changed would move a piece
         * that is no longer there. The next tick searches again. */
        if (state->game.game_over || key != game_hash(&state->game)) return 0;

        apply_engine_result(state, res, state->thinking_by);
        return 1;
    }

    int side = state->game.pos.side;
    if (!players_should_start(state->players, side, state->paused,
                              state->game.game_over, now_ms() - state->last_move_ms))
        return 0;

    char by[32];
    player_label(&state->players[side], by, sizeof(by));
    start_thinking(state, state->drivers[side], by);
    return 0;
}

int handle_command(TUIState *state, const char *cmd) {
    if (!cmd || !cmd[0]) return 1;

    /* Like UCI's "stop": play the best move found so far. */
    if (strcmp(cmd, "stop") == 0) {
        if (!state->thinking) {
            snprintf(state->status, sizeof(state->status), "No search in progress");
            return 1;
        }
        opponent_stop(state->thinking);
        drive_turn(state);
        return 1;
    }
    if (strcmp(cmd, "pause") == 0) {
        cancel_engine_search(state);
        state->paused = 1;
        snprintf(state->status, sizeof(state->status),
                 "Paused — Space or 'resume' to continue, 'go' for one move");
        return 1;
    }
    if (strcmp(cmd, "resume") == 0) {
        state->paused = 0;
        snprintf(state->status, sizeof(state->status), "Resumed");
        return 1;
    }

    /* Moves/"go"/"eval" are rejected while an engine thinks; none of
     * them mean "start over". Everything else is safe to run. */
    int is_move = (cmd[0] >= 'a' && cmd[0] <= 'h' && cmd[1] >= '1' && cmd[1] <= '8');
    int reject_while_thinking =
        is_move ||
        strcmp(cmd, "go") == 0 ||
        strcmp(cmd, "eval") == 0;

    if (reject_while_thinking && state->thinking) {
        snprintf(state->status, sizeof(state->status),
                 "Engine is thinking — please wait, or use 'stop'");
        return 1;
    }

    /* These replace the game state, so a search for the old position is
     * not worth waiting for. */
    if (strcmp(cmd, "new") == 0 ||
        strcmp(cmd, "undo") == 0 || strcmp(cmd, "u") == 0 ||
        strncmp(cmd, "loadfen ", 8) == 0) {
        cancel_engine_search(state);
    }

    if (is_move) {
        if (state->game.game_over) return 1;
        if (!tui_can_move_by_hand(state)) {
            snprintf(state->status, sizeof(state->status),
                     "It is the engine's move — 'pause' to move for it");
            return 1;
        }
        try_move(state, cmd);
        return 1;
    }
    if (strcmp(cmd, "go") == 0) {
        if (state->game.game_over) return 1;
        int side = state->game.pos.side;
        char by[32];
        if (state->drivers[side]) {
            player_label(&state->players[side], by, sizeof(by));
            start_thinking(state, state->drivers[side], by);
        } else {
            Player m = player_builtin(DIFF_MEDIUM);
            player_label(&m, by, sizeof(by));
            start_thinking(state, state->go_driver, by);
        }
        return 1;
    }

    char err[128];
    Player before[2] = { state->players[WHITE], state->players[BLACK] };
    int pc = players_apply_command(state->players, cmd, err, sizeof(err));
    if (pc < 0) {
        snprintf(state->status, sizeof(state->status), "%s", err);
        return 1;
    }
    if (pc > 0) {
        for (int side = WHITE; side <= BLACK; side++)
            if (!same_player(&before[side], &state->players[side]))
                attach(state, side);
        char setup[128];
        describe_setup(state, setup, sizeof(setup));
        snprintf(state->status, sizeof(state->status), "%s", setup);
        return 1;
    }

    if (strncmp(cmd, "depth ", 6) == 0) {
        int d = atoi(cmd + 6);
        if (d < 1 || d > 8) {
            snprintf(state->status, sizeof(state->status), "Depth must be 1–8");
            return 1;
        }
        int engines = 0;
        for (int side = WHITE; side <= BLACK; side++) {
            if (state->players[side].kind != PLAYER_BUILTIN) continue;
            state->players[side].depth = d;
            attach(state, side);
            engines++;
        }
        if (engines)
            snprintf(state->status, sizeof(state->status), "Engine depth cap set to %d", d);
        else
            snprintf(state->status, sizeof(state->status),
                     "No engine is playing; depth unchanged");
        return 1;
    }
    if (strcmp(cmd, "undo") == 0 || strcmp(cmd, "u") == 0) {
        tui_undo(state);
        return 1;
    }
    if (strcmp(cmd, "new") == 0) {
        tui_new_game(state);
        return 1;
    }
    if (strcmp(cmd, "pgn") == 0 || strncmp(cmd, "pgn ", 4) == 0) {
        char path[512];
        if (cmd[3] == ' ' && cmd[4])
            snprintf(path, sizeof(path), "%s", cmd + 4);
        else
            pgn_default_path(path, sizeof(path));

        char white[48], black[48];
        players_pgn_name(state->players, WHITE, white, sizeof(white));
        players_pgn_name(state->players, BLACK, black, sizeof(black));
        PgnHeader h = {
            .event = "Casual game",
            .site  = "dchess",
            .white = white,
            .black = black,
        };

        if (pgn_write(&state->game, &h, path) == 0)
            snprintf(state->status, sizeof(state->status), "Saved PGN: %.200s", path);
        else
            snprintf(state->status, sizeof(state->status),
                     "Could not write PGN to %.200s", path);
        return 1;
    }
    if (strcmp(cmd, "fen") == 0) {
        char buf[FEN_BUFSIZE];
        int fullmove = state->game.move_count / 2 + 1;
        position_to_fen(&state->game.pos, state->game.halfmove_clock, fullmove, buf, sizeof(buf));
        snprintf(state->status, sizeof(state->status), "FEN: %s", buf);
        return 1;
    }
    if (strncmp(cmd, "loadfen ", 8) == 0) {
        if (!game_load_fen(&state->game, cmd + 8)) {
            snprintf(state->status, sizeof(state->status),
                     "Invalid FEN, position unchanged");
            return 1;
        }
        state->selected = 0;
        memset(state->highlight, 0, sizeof(state->highlight));
        memset(&state->last_search, 0, sizeof(state->last_search));
        state->last_search_by[0] = '\0';
        snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");
        snprintf(state->status, sizeof(state->status), "Position loaded from FEN");
        return 1;
    }
    if (strncmp(cmd, "theme ", 6) == 0) {
        int t = theme_from_name(cmd + 6);
        if (t < 0) {
            char names[96] = "";
            for (int i = 0; i < theme_count(); i++) {
                strncat(names, theme_name(i), sizeof(names) - strlen(names) - 1);
                if (i + 1 < theme_count())
                    strncat(names, " | ", sizeof(names) - strlen(names) - 1);
            }
            snprintf(state->status, sizeof(state->status),
                     "Unknown theme '%.40s'. Use: %s", cmd + 6, names);
            return 1;
        }
        state->theme = t;
        init_colors(t);
        if (state->request_redraw) state->request_redraw(state->redraw_ctx);
        snprintf(state->status, sizeof(state->status), "Theme: %s", theme_name(t));
        return 1;
    }
    if (strcmp(cmd, "flip") == 0) {
        state->view_side = (state->view_side == WHITE) ? BLACK : WHITE;
        state->selected = 0;
        memset(state->highlight, 0, sizeof(state->highlight));
        snprintf(state->status, sizeof(state->status), "Board turned: %s at the bottom",
                 state->view_side == WHITE ? "White" : "Black");
        return 1;
    }
    if (strcmp(cmd, "eval") == 0) {
        SearchResult res = search(&state->game.pos, 1, 0);
        int score_white = eval_white_view(res.best_score, state->game.pos.side);
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", score_white / 100.0f);
        snprintf(state->status, sizeof(state->status), "Eval: %s", state->last_eval);
        return 1;
    }
    if (strcmp(cmd, "stats") == 0) {
        stats_load(&state->stats);
        int total = state->stats.games_played[0] +
                    state->stats.games_played[1] +
                    state->stats.games_played[2];
        int wins  = state->stats.wins[0] +
                    state->stats.wins[1] +
                    state->stats.wins[2];
        snprintf(state->status, sizeof(state->status),
                 "Stats: %d games, %d wins (%.0f%%) | run dchess --stats for full view",
                 total, wins,
                 total ? 100.0f * wins / total : 0.0f);
        return 1;
    }
    if (strcmp(cmd, "help") == 0) {
        snprintf(state->status, sizeof(state->status),
                 "e2e4 go stop pause resume undo new swap flip depth N eval fen pgn "
                 "loadfen stats quit | white|black human|engine [level]");
        return 1;
    }
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) return -1;

    snprintf(state->status, sizeof(state->status), "Unknown: '%s' (type 'help')", cmd);
    return 1;
}
```

- [ ] **Step 5: Update `tui.c`**

In `src/tui/tui.c`:

1. **Recording stats** in `show_game_over_popup`. Replace the whole block from `/* Save stats for this completed game  */` through its closing `}` with:

```c
    /* Only games against the built-in engine have a place in the stats. */
    {
        int human, level;
        if (players_stats_entry(state->players, &human, &level)) {
            int result = 0;
            const char *r = state->game.result;
            if (strstr(r, "White wins"))      result = (human == WHITE) ? 1 : -1;
            else if (strstr(r, "Black wins")) result = (human == BLACK) ? 1 : -1;
            int total_secs = state->game.white_clock + state->game.black_clock;
            stats_record(&state->stats, level, result, human,
                         state->game.move_count, total_secs);
            stats_save(&state->stats);
        }
    }
```

2. **Quitting from the popup.** In the `'q'` branch of that popup, replace `cancel_engine_search(state);` with `tui_release_players(state);` and keep the comment above it.

3. **After a move made with the cursor.** At the end of `move_to_square`, replace:

```c
    if (state->two_player) {
        state->view_side  = state->game.pos.side;
        state->cursor_row = 6;
        state->cursor_col = 4;
    } else if (state->engine_side == state->game.pos.side) {
        /* Only kicks off the search; the main loop applies the result. */
        handle_command(state, "go");
    }
```
with:
```c
    /* An engine's reply is drive_turn()'s job. */
    if (!players_automated(state->players, WHITE) &&
        !players_automated(state->players, BLACK)) {
        state->view_side  = state->game.pos.side;
        state->cursor_row = 6;
        state->cursor_col = 4;
    }
```

4. **`tui_init`.** Delete the mutex comment block and `pthread_mutex_init(&state->search_mutex, NULL);`. Replace the Task 3 bridge lines, from `const Player *pl = …` through `state->engine_depth = cli_depth_for_difficulty(state->difficulty);`, and the line `state->time_limit_ms = cli_time_limit_for_difficulty(state->difficulty);` with:

```c
    state->players[WHITE] = args ? args->players[WHITE] : player_human();
    state->players[BLACK] = args ? args->players[BLACK] : player_builtin(DIFF_MEDIUM);
```
Also delete the block:
```c
    /* Engine plays the opposite side of the human (disabled in two-player) */
    state->engine_side = state->two_player ? -1 :
                         (state->player_side == WHITE) ? BLACK : WHITE;
```

5. **Enter in `handle_key`.** Replace:

```c
            if (state->search_running) {
                snprintf(state->status, sizeof(state->status),
                         "Engine is thinking — please wait, or use 'stop'");
            } else if (!state->game.game_over) {
```
with:
```c
            if (state->thinking) {
                snprintf(state->status, sizeof(state->status),
                         "Engine is thinking — please wait, or use 'stop'");
            } else if (!tui_can_move_by_hand(state)) {
                snprintf(state->status, sizeof(state->status),
                         "It is the engine's move — 'pause' to move for it");
            } else if (!state->game.game_over) {
```

6. **Space in `handle_key`.** After the `case 'u':` block, add:

```c
        case ' ':
            handle_command(state, state->paused ? "resume" : "pause");
            break;
```

7. **The main loop in `tui_run`.** Replace:

```c
    /* Engine moves first if it already has the move. */
    if (!state->two_player && state->engine_side == state->game.pos.side)
        handle_command(state, "go");
```
with `tui_attach_players(state);`. Then replace:

```c
        if (poll_engine_search(state))
            game_update_status(&state->game);
```
with:
```c
        if (drive_turn(state))
            game_update_status(&state->game);
```
At the end of `tui_run`, replace `cancel_engine_search(state);` with `tui_release_players(state);` and keep its comment.

- [ ] **Step 6: Update onboarding, the engine panel and Space**

In `src/tui/onboard.c`, `tui_onboarding`, replace:

```c
    choice.side           = state->two_player ? SIDE_TWO_PLAYER : state->player_side;
    choice.difficulty     = state->difficulty;
```
with the following. Task 5 replaces it.

```c
    int w_human = state->players[WHITE].kind == PLAYER_HUMAN;
    int b_human = state->players[BLACK].kind == PLAYER_HUMAN;
    choice.side       = (w_human && b_human) ? SIDE_TWO_PLAYER : (w_human ? WHITE : BLACK);
    choice.difficulty = state->players[choice.side == BLACK ? WHITE : BLACK].level;
```

In `src/tui/panels.c`, `draw_engine_panel`, replace:

```c
    if (state->two_player || state->engine_side < 0) {
```
with:
```c
    if (!players_automated(state->players, WHITE) &&
        !players_automated(state->players, BLACK) &&
        !state->thinking && state->last_search.nodes == 0) {
```
and replace `if (state->search_running) {` with `if (state->thinking) {`.

In `src/tui/input.c`, `read_key`, in the normal-mode `switch`, add before `case 'i':`:

```c
            case ' ':   /* pause / resume */
                return ' ';
```
and add ` *     * Space              -> pause / resume (returned to caller)` to the header comment's normal-mode list, after the `'u'` line.

- [ ] **Step 7: Update the help text and README**

In `src/utils/cli.c`, `cli_help`, replace the in-game command lines for `go`, `undo / u` and `flip`:

```c
        "    go          Let the engine play the current side\n"
```
becomes
```c
        "    go          Play one engine move for the side to move,\n"
        "                even while paused\n"
```
The two `undo / u` lines become:
```c
        "    undo / u    Take back your last move. Against the engine this\n"
        "                takes back its reply too, so the turn returns to you.\n"
        "                Between two engines it takes back one and pauses\n"
```
`"    flip        Swap which side the engine plays\n"` becomes:
```c
        "    flip        Turn the board around\n"
        "    pause / resume\n"
        "                Hold and restart the engines (Space does both)\n"
        "    white|black human\n"
        "    white|black engine [easy|medium|hard]\n"
        "                Change who plays a side, mid-game\n"
        "    swap        Exchange the two players\n"
```
In the `CURSOR CONTROLS` block, after the `u` line, add:
```c
        "    Space                    Pause / resume the engines\n"
```

In `README.md`:
- In the normal-mode block, after `Tab         open in-game stats popup (any key to close)`, add `Space       pause / resume the engines`.
- Replace `go          let the engine play the current side` with `go          play one engine move for the side to move, even while paused`.
- Replace `flip        swap which side the engine plays` with:

```
flip        turn the board around
pause / resume
            hold and restart the engines (Space does both)
white|black human
white|black engine [easy|medium|hard]
            change who plays a side, mid-game
swap        exchange the two players
```
- Replace `- Engine plays one side, human the other — configurable at launch or mid-game` with `- Each side is you, a friend or the engine at any level — including engine against engine — configurable at launch or mid-game`.

- [ ] **Step 8: Build and run the suite**

Run: `make -B dchess 2>&1 | tee build/task4.log | tail -3; grep -c warning build/task4.log; make -B test 2>&1 | tail -3`
Expected: the build succeeds, `0` warnings, and `All suites passed.`

- [ ] **Step 9: Verify the turn flow in tmux**

Run each scenario and read the capture. Every scenario starts with `tmux kill-session -t op 2>/dev/null` and ends with `tmux kill-session -t op`.

**A. You against the engine, unchanged.**
```bash
tmux new-session -d -s op -x 120 -y 40 "./dchess --no-menu"
sleep 1; tmux send-keys -t op i 'e2e4' Enter; sleep 5
tmux capture-pane -p -t op | tail -25
tmux send-keys -t op i 'undo' Enter; sleep 1; tmux capture-pane -p -t op | tail -6
```
Expected: the moves panel shows `1. e4` followed by Black's reply, and the status reads `dchess Medium: …`. After undo the status says `Took back 2 moves` and the moves panel is empty.

**B. Engine against engine: auto-play, pause, step, resume, and refusing hand moves.**
```bash
tmux new-session -d -s op -x 120 -y 40 "./dchess --white easy --black easy"
sleep 4; tmux capture-pane -p -t op > build/b1.txt
tmux send-keys -t op i 'e2e4' Enter; sleep 0.3; tmux capture-pane -p -t op | tail -4
tmux send-keys -t op ' '; sleep 1; tmux capture-pane -p -t op > build/b2.txt
sleep 2; tmux capture-pane -p -t op > build/b3.txt; diff <(grep -v ':[0-9][0-9]' build/b2.txt) <(grep -v ':[0-9][0-9]' build/b3.txt) && echo PAUSED-STABLE
tmux send-keys -t op i 'go' Enter; sleep 2; tmux capture-pane -p -t op | tail -4
tmux send-keys -t op i 'undo' Enter; sleep 1; tmux capture-pane -p -t op | tail -4
tmux send-keys -t op ' '; sleep 3; tmux capture-pane -p -t op | tail -25
```
Expected:
- `build/b1.txt` shows several moves.
- The `e2e4` line reports `It is the engine's move — 'pause' to move for it`, unless the game happened to be paused.
- `PAUSED-STABLE` is printed. The `grep -v` drops the clock lines, which change even while paused.
- `go` plays exactly one move while paused.
- `undo` reports `Took back 1 move — paused`.
- After Space the moves keep coming.

**C. Two humans.**
```bash
tmux new-session -d -s op -x 120 -y 40 "./dchess -2"
sleep 1; tmux send-keys -t op i 'e2e4' Enter; sleep 2; tmux capture-pane -p -t op | tail -25
tmux send-keys -t op i 'undo' Enter; sleep 1; tmux capture-pane -p -t op | tail -4
```
Expected: no engine reply appears after `1. e4`, and undo says `Took back 1 move`.

**D. Changing a player mid-search, then `swap`.**
```bash
tmux new-session -d -s op -x 120 -y 40 "./dchess --white human --black hard"
sleep 1; tmux send-keys -t op i 'e2e4' Enter; sleep 0.5
tmux send-keys -t op i 'black human' Enter; sleep 6; tmux capture-pane -p -t op | tail -25
tmux send-keys -t op i 'black engine easy' Enter; sleep 3; tmux capture-pane -p -t op | tail -6
tmux send-keys -t op i 'swap' Enter; sleep 3; tmux capture-pane -p -t op | tail -6
```
Expected:
- After `black human`, six seconds later there is still only `1. e4`: Hard's search was cancelled and no move landed. The status reads `White: You · Black: You`.
- After `black engine easy`, Black replies.
- After `swap`, the status reads `White: dchess Easy · Black: You`. It is White's (the engine's) turn only if Black just moved, so the engine either moves or waits for you; either is correct.

**E. `stop` and `depth`.**
```bash
tmux new-session -d -s op -x 120 -y 40 "./dchess --white human --black hard"
sleep 1; tmux send-keys -t op i 'e2e4' Enter; sleep 1
tmux send-keys -t op i 'stop' Enter; sleep 1; tmux capture-pane -p -t op | tail -6
tmux send-keys -t op i 'depth 3' Enter; sleep 1; tmux capture-pane -p -t op | tail -3
```
Expected: `stop` makes Black play at once, with the status `dchess Hard: …`. `depth 3` reports `Engine depth cap set to 3`.

**F. Quitting mid-search (Review Focus 1).**
```bash
tmux new-session -d -s op -x 120 -y 40 "./dchess --white hard --black hard; echo EXITED; sleep 5"
sleep 2; tmux send-keys -t op i 'quit' Enter; sleep 1; tmux capture-pane -p -t op | tail -3
```
Expected: `EXITED` appears within a second.

**G. An engine-vs-engine game that ends (Review Focus 2).**
```bash
cp ~/.local/share/dchess/stats.dat build/stats.before 2>/dev/null || true
tmux new-session -d -s op -x 120 -y 40 "./dchess --white easy --black easy --fen '6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1'"
sleep 3; tmux capture-pane -p -t op | tail -25
cmp ~/.local/share/dchess/stats.dat build/stats.before && echo STATS-UNCHANGED
tmux send-keys -t op r; sleep 4; tmux capture-pane -p -t op | tail -25
```
Expected:
- The game-over popup shows `White wins`.
- `STATS-UNCHANGED` is printed. If there was no stats file before, `cmp` fails; check instead that none was created.
- After `r`, a new game from the standard position is already auto-playing.

- [ ] **Step 10: Commit**

```bash
git add headers/tui/tui.h headers/tui/commands.h src/tui/commands.c src/tui/tui.c src/tui/onboard.c src/tui/panels.c src/tui/input.c src/utils/cli.c README.md
git commit -m "feat(tui): drive both sides from the player table, with pause and player commands"
```

---

### Task 5: White and Black rows in the start menu

**Files:**
- Modify: `src/tui/onboard.c`

**Interfaces:**
- Consumes: `Player`, `player_human()`, `player_builtin(int)`, `player_label()` from Task 1; `state->players` from Task 4.
- Produces: the onboarding screen writes `chosen.players[2]` straight from the two rows.

- [ ] **Step 1: Replace the model at the top of the file**

In `src/tui/onboard.c`, replace everything from `enum { ROW_SIDE, ROW_DIFFICULTY, …` through the end of `diff_label()` with:

```c
enum { ROW_WHITE, ROW_BLACK, ROW_POSITION, ROW_THEME, ROW_START, ROW_COUNT };

/* Each player row cycles You, then the engine at each level. */
#define WHO_COUNT 4

typedef struct {
    int  who[2];          /* 0 = You, 1 + DIFF_* = the engine */
    int  use_custom_fen;
    char fen[128];
    int  theme;
} OnboardChoice;

static Player who_player(int who)
{
    return who == 0 ? player_human() : player_builtin(who - 1);
}

static int player_who(const Player *p)
{
    return p->kind == PLAYER_HUMAN ? 0 : p->level + 1;
}
```

- [ ] **Step 2: Pre-fill the choice from the current players**

Replace the Task 4 bridge lines and the two lines that follow:

```c
    int w_human = state->players[WHITE].kind == PLAYER_HUMAN;
    int b_human = state->players[BLACK].kind == PLAYER_HUMAN;
    choice.side       = (w_human && b_human) ? SIDE_TWO_PLAYER : (w_human ? WHITE : BLACK);
    choice.difficulty = state->players[choice.side == BLACK ? WHITE : BLACK].level;
```
with:
```c
    choice.who[WHITE] = player_who(&state->players[WHITE]);
    choice.who[BLACK] = player_who(&state->players[BLACK]);
```

Change `int cursor_row = ROW_SIDE;` to `int cursor_row = ROW_WHITE;`.

- [ ] **Step 3: Draw the two rows**

Replace from `int diff_dim = (choice.side == SIDE_TWO_PLAYER);` through the line `else if (cursor_row == ROW_DIFFICULTY) wattroff(win, A_REVERSE);` with:

```c
        const char *row_name[2] = { "White:          ", "Black:          " };
        for (int side = WHITE; side <= BLACK; side++) {
            Player p = who_player(choice.who[side]);
            char label[32];
            player_label(&p, label, sizeof(label));
            int on = (cursor_row == (side == WHITE ? ROW_WHITE : ROW_BLACK));
            if (on) wattron(win, A_REVERSE);
            mvwprintw(win, 2 + side * 2, 3, "%s%-*.*s", row_name[side],
                      content_w, content_w, label);
            if (on) wattroff(win, A_REVERSE);
        }
```

In the geometry comment, change the example `"Play as:        "` to `"White:          "`.

- [ ] **Step 4: Handle the keys**

In the key `switch`:
- **Up and down:** delete the two `if (cursor_row == ROW_DIFFICULTY && diff_dim)` statements. Each is two lines, one in `KEY_UP` and one in `KEY_DOWN`.
- **`KEY_LEFT`:** replace the `ROW_SIDE` and `ROW_DIFFICULTY` branches, down to the `else if (cursor_row == ROW_POSITION)` line (not included), with:

```c
                if (cursor_row == ROW_WHITE || cursor_row == ROW_BLACK) {
                    int s = (cursor_row == ROW_WHITE) ? WHITE : BLACK;
                    choice.who[s] = (choice.who[s] + WHO_COUNT - 1) % WHO_COUNT;
                }
```
- **`KEY_RIGHT`:** replace the same two branches with:

```c
                if (cursor_row == ROW_WHITE || cursor_row == ROW_BLACK) {
                    int s = (cursor_row == ROW_WHITE) ? WHITE : BLACK;
                    choice.who[s] = (choice.who[s] + 1) % WHO_COUNT;
                }
```

In both cases, the `else if (cursor_row == ROW_POSITION)` that follows stays attached to the new `if`.

- [ ] **Step 5: Hand the players over**

At the end of `tui_onboarding`, replace the Task 3 block from `if (choice.side == SIDE_TWO_PLAYER) {` through its closing `}` with:

```c
    chosen.players[WHITE] = who_player(choice.who[WHITE]);
    chosen.players[BLACK] = who_player(choice.who[BLACK]);
```

- [ ] **Step 6: Build and run the suite**

Run: `make -B dchess 2>&1 | tee build/task5.log | tail -3; grep -c warning build/task5.log; make -B test 2>&1 | tail -3`
Expected: `0` warnings and `All suites passed.` Also run `grep -n "SIDE_TWO_PLAYER\|diff_label\|side_label\|ROW_SIDE\|ROW_DIFFICULTY" src/tui/onboard.c`, which must print nothing.

- [ ] **Step 7: Verify in tmux**

```bash
tmux kill-session -t op 2>/dev/null
tmux new-session -d -s op -x 120 -y 40 "./dchess --menu -d hard"
sleep 1; tmux capture-pane -p -t op | grep -E "White:|Black:"
tmux send-keys -t op Right; sleep 0.3; tmux capture-pane -p -t op | grep -E "White:"
tmux send-keys -t op Enter; sleep 0.3; tmux send-keys -t op Enter; sleep 4
tmux capture-pane -p -t op | tail -25
tmux kill-session -t op
```
Expected:
- The menu opens with `White:          You` and `Black:          dchess Hard`.
- One Right on the White row gives `dchess Easy`.
- The two Enters (the first jumps to Start, the second starts) begin an engine-vs-engine game that plays on its own.

- [ ] **Step 8: Commit**

```bash
git add src/tui/onboard.c
git commit -m "feat(tui): pick White and Black separately in the start menu"
```

---

### Task 6: Matchup title and engine panel

**Files:**
- Modify: `src/tui/render.c` (`render_all`, board title and link)
- Modify: `src/tui/panels.c` (`draw_engine_panel`)

**Interfaces:**
- Consumes: `players_matchup()`, `players_automated()` from Task 1; `state->thinking`, `thinking_by`, `last_search_by`, `paused` from Task 4.
- Produces: nothing new.

- [ ] **Step 1: Board title**

In `src/tui/render.c`, `render_all`, replace:

```c
    werase(board);
    panel_frame(board, "dchess", CP_ACC_BOARD);

    int bh, bw;
    getmaxyx(board, bh, bw);
    const char *brand = " github.com/DDumbying ";
    if (bw > (int)strlen(brand) + 12) {
```
with:
```c
    werase(board);
    char title[64];
    players_matchup(state->players, title, sizeof(title));
    panel_frame(board, title, CP_ACC_BOARD);

    int bh, bw;
    getmaxyx(board, bh, bw);
    /* The link gives way to the matchup when both do not fit. */
    const char *brand = " github.com/DDumbying ";
    if (bw > (int)(strlen(title) + strlen(brand)) + 8) {
```

- [ ] **Step 2: Engine panel**

In `src/tui/panels.c`, replace `draw_engine_panel`, from its signature down to (but not including) the line `char depth[16], nodes[16], nps[16];`, with:

```c
static void draw_engine_panel(WINDOW *p, const TUIState *state)
{
    const char *by = state->thinking ? state->thinking_by : state->last_search_by;
    char title[48];
    if (by[0]) snprintf(title, sizeof(title), "engine · %s", by);
    else       snprintf(title, sizeof(title), "engine");
    panel_frame(p, title, CP_ACC_ENGINE);

    if (state->thinking) {
        wattron(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        mvw_clip(p, 1, 2, "thinking...");
        wattroff(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        return;
    }
    if (state->paused && players_automated(state->players, state->game.pos.side)) {
        wattron(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        mvw_clip(p, 1, 2, "paused");
        wattroff(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        return;
    }

    const SearchResult *r = &state->last_search;
    if (r->nodes == 0) {
        int any = players_automated(state->players, WHITE) ||
                  players_automated(state->players, BLACK);
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, any ? "waiting" : "no engine");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

```

- [ ] **Step 3: Build and run the suite**

Run: `make -B dchess 2>&1 | tee build/task6.log | tail -3; grep -c warning build/task6.log; make -B test 2>&1 | tail -3`
Expected: `0` warnings and `All suites passed.`

- [ ] **Step 4: Verify the display in tmux at two sizes**

```bash
for size in "120 40" "60 20"; do
  set -- $size
  tmux kill-session -t op 2>/dev/null
  tmux new-session -d -s op -x $1 -y $2 "./dchess --white easy --black hard"
  sleep 4; tmux capture-pane -p -t op | head -3
  tmux capture-pane -p -t op | grep -n "engine" | head -3
  tmux send-keys -t op ' '; sleep 1; tmux capture-pane -p -t op | grep -n "paused" | head -2
  tmux kill-session -t op
done
tmux new-session -d -s op -x 120 -y 40 "./dchess -2"
sleep 1; tmux capture-pane -p -t op | head -2; tmux capture-pane -p -t op | grep -n "no engine"
tmux kill-session -t op
```
Expected:
- The top border reads `dchess Easy vs dchess Hard`.
- At 120 columns, `github.com/DDumbying` also appears on the right. At 60 columns the link is gone and the title is intact.
- The engine panel title is `engine · dchess …`.
- After Space, the panel reads `paused` when an engine is to move. The side column is hidden at 60 columns, so check `paused` only at 120.
- `-2` shows `Player 1 vs Player 2` and `no engine`.

- [ ] **Step 5: Resize during auto-play (Review Focus 5)**

```bash
tmux kill-session -t op 2>/dev/null
tmux new-session -d -s op -x 120 -y 40 "./dchess --white easy --black easy"
sleep 2; tmux resize-window -t op -x 30 -y 15; sleep 1; tmux capture-pane -p -t op | head -3
tmux resize-window -t op -x 100 -y 30; sleep 3; tmux capture-pane -p -t op | tail -20
tmux kill-session -t op
```
Expected: at 30x15 the screen says `Terminal too small`. Back at 100x30 the dashboard redraws and moves continue to appear. There is no crash.

- [ ] **Step 6: Commit**

```bash
git add src/tui/render.c src/tui/panels.c
git commit -m "feat(tui): show the matchup and which engine is thinking"
```
