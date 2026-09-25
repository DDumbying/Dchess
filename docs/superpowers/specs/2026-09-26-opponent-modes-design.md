# Opponent modes

**Status:** approved design, 2026-09-26
**Branch:** `feat/opponent-modes`

## Goal

Either side of the board can be played by anyone: you, a second person at the
same keyboard, or an engine. That covers you against the engine, two people
taking turns, and engine against engine, with the players changeable
mid-game. The engine side goes through a driver interface, so project B's UCI
engines plug in without touching the TUI.

This is project A of five (A opponent modes, B UCI engines, C profiles,
D stats page, E opening book).

## Decisions

| Question | Decision |
|---|---|
| Model | Per-side players: White and Black are each human or an engine |
| Engine vs engine | Supported; plays automatically, pausable |
| Changing players mid-game | Yes, through commands |
| Architecture | Player table and a driver interface, both in core so they can be unit-tested |

## Player model

A new core header, `headers/game/players.h`:

```c
typedef enum { PLAYER_HUMAN, PLAYER_BUILTIN } PlayerKind;   /* PLAYER_UCI arrives in B */
typedef struct { PlayerKind kind; int level; int depth; int time_ms; } Player;
```

`level` is `DIFF_EASY`, `DIFF_MEDIUM` or `DIFF_HARD`. `depth` and `time_ms`
start at that level's values (`cli_depth_for_difficulty`,
`cli_time_limit_for_difficulty`) and can be overridden by `depth N`. All
three are ignored for a human.

`TUIState` gains `Player players[2]`, indexed by `WHITE` and `BLACK`, and a
`paused` flag. These fields are removed: `engine_side`, `two_player`,
`player_side`, `difficulty`, `engine_depth`, `time_limit_ms`.

## Player rules

`src/game/players.c`, all pure functions:

- `int players_automated(const Player p[2], int side)`: 1 when that side
  moves by itself (anything but a human).
- `int players_undo_plies(const Player p[2], int side_to_move, int undo_count)`:
  how many plies `undo` takes back, or 0 when there is not enough history.

  | Pairing | Plies |
  |---|---|
  | One human, one engine | Back to the human's turn: 1 if the engine is to move, else 2 |
  | Two humans | 1 |
  | Two engines | 1 |

- `int players_apply_command(Player p[2], const char *cmd, char *err, size_t n)`:
  returns 1 if `cmd` was a player command and it was applied, 0 if it was not a
  player command, and -1 with a message in `err` if it was one but invalid. It
  accepts:
  - `white human`, `black human`
  - `white engine`, `black engine`, which default to Medium
  - `white engine <easy|medium|hard>`, and the same for black
  - `swap`, which exchanges the two players
- `void player_label(const Player *p, char *buf, size_t n)`: `You` for a
  human, `dchess Hard` for the built-in engine.
- `void players_pgn_name(const Player p[2], int side, char *buf, size_t n)`:
  the PGN tag value. A human is `Player`, or `Player 1` (White) and
  `Player 2` (Black) when both sides are human. The built-in engine is
  `dchess (Hard)`, as today.

## Opponent driver

`src/game/opponent.c`:

```c
typedef struct Opponent Opponent;

Opponent *opponent_builtin(int depth, int time_ms);
int  opponent_start (Opponent *, const Position *pos, U64 key);   /* non-blocking */
int  opponent_poll  (Opponent *, SearchResult *out, U64 *key);    /* 1 once a result is ready */
void opponent_cancel(Opponent *);
void opponent_free  (Opponent *);
```

- `opponent_start` returns 0, and does nothing, while a search is already
  running.
- `opponent_poll` returns 0 before a start and while the search is running.
  Once it has returned 1 the driver is idle again.
- `opponent_cancel` stops the search and waits for its thread. A search
  cancelled this way produces no result.
- The key handed to `start` is returned with the result. The caller applies
  the move only if the live game still hashes to it.

The built-in implementation takes over the thread, mutex, snapshot and
handoff code from `commands.c` unchanged in behaviour, including the
synchronous fallback when `pthread_create` fails. Project B adds
`opponent_uci(...)` behind the same functions.

There is one driver per engine side, built from that side's `Player`. It is
rebuilt whenever the player changes. Only the side to move ever searches, so
at most one search runs at a time; this matters because `search()` uses
global tables.

## Turn flow

A single `drive_turn(state)` in `commands.c` replaces the four places that
start the engine today (after a human move, at startup, after a stale
result, and `go`). The main loop calls it on every 100 ms input tick:

1. If the side to move's driver has a result, apply it when its key matches
   the live game: eval record, `last_search`, `game_play`, status line. A
   stale result is dropped.
2. Otherwise, start the side to move's driver if all of these hold:
   - the game is not over;
   - it is not paused;
   - the side to move is an engine;
   - its driver is idle;
   - when **both** sides are engines, at least 500 ms have passed since the
     last move.

   Against a human the engine replies immediately.

### Commands and keys

- `pause` and `resume`, and **Space** in normal mode, toggle `paused`.
  Pausing cancels the search in flight.
- `stop` cancels the search and pauses. Without the pause, the next tick
  would restart it.
- `go` plays one move for the side to move, even when paused, so it works as
  a single step. On an engine's turn it uses that side's driver. On a human's
  turn it uses a built-in Medium driver kept for that purpose.
- `undo` takes back `players_undo_plies` plies. When both sides are engines
  it also pauses.
- `new` keeps the players and clears `paused`.
- The player commands from `players_apply_command` cancel the search of any
  side they change and rebuild that side's driver.
- `depth N` sets `depth` on every built-in player and rebuilds their drivers.
- `flip` only turns the board. Its old job, cycling the engine's side, is
  covered by the player commands.
- When both sides are human, the board still turns to face the side to move
  after every move, as two-player mode does today.

## Start menu

The onboarding rows "Play as" and "Difficulty" become **White** and
**Black**. Each cycles `You` → `dchess Easy` → `dchess Medium` →
`dchess Hard` with ←/→. The defaults are White `You` and Black
`dchess Medium`, the same game as today's default. Position, Theme and Start
are unchanged, and the row count stays at five.

## Command line

- `--white <human|easy|medium|hard>` and `--black <human|easy|medium|hard>`
  are new. Either one counts as a gameplay flag, so the menu is skipped, as
  it is for the existing flags.
- `-c`, `-d` and `-2` stay. They are applied first, then `--white` and
  `--black` override their side, whatever the order on the command line.
- `--help` documents the new flags, with `dchess --white hard --black hard`
  as an example.

`CliArgs` carries the result as `Player players[2]`, replacing `player_side`,
`difficulty`, `engine_depth` and `two_player`.

## Display

- **Board panel title:** the matchup, White first, from `player_label`:
  `You vs dchess Hard`, `dchess Easy vs dchess Hard`. Two humans show as
  `Player 1 vs Player 2`. It is truncated to fit and stays visible when the
  side column is hidden.
- **Engine panel:**
  - Its title names the engine behind the last search: `engine · dchess Hard`.
  - It shows `thinking...` while a search runs.
  - It shows `paused` when paused with an engine to move.
  - Otherwise it shows the last search, whenever one has run.
  - `no engine` appears only when both sides are human and nothing has been
    searched.
- **Default status line:** `White: You · Black: dchess Medium`.

## Stats

A finished game is recorded only when, at the end, exactly one side is human
and the other is the built-in engine. The difficulty recorded is that engine's
level, and the colour is the human's. Games between two humans and
engine-vs-engine games are not recorded. The stats file format does not
change; project C replaces it.

## PGN

The White and Black tags come from `players_pgn_name`. `pgn_write` itself
does not change.

## Testing

- **`tests/test_players.c`:**
  - `players_undo_plies` for all four pairings, for each side to move, and
    with too little history.
  - `players_apply_command`: each valid form, `swap`, an unknown level, an
    unknown colour, a level given for `human`, and a non-player command
    returning 0.
  - `player_label`, `players_pgn_name` and `players_automated`.
- **`tests/test_opponent.c`:**
  - the built-in driver finds a mate in one through start and poll;
  - the result carries the key it was started with;
  - cancel returns promptly and produces no result;
  - poll before any start returns 0;
  - a second start while running is refused.
- **The existing suites and perft** still pass.
- **tmux captures:**
  - you against the engine as each colour, where behaviour is unchanged;
  - engine against engine playing on its own, then Space to pause, `go` to
    step, and resume;
  - two humans;
  - `swap` and `black human` mid-game;
  - `undo` in each pairing.

## Out of scope

- UCI engines, which are project B.
- Time controls and separate clocks per player.
- Profiles and the new stats page, which are projects C and D.
- Showing the players anywhere beyond the board title, engine panel and
  status line.
