# UCI engines

**Status:** approved design, 2026-09-26
**Branch:** `feat/uci-engines`, from `feat/opponent-modes` (PR #6)

## Goal

Play against, or watch, any external engine that speaks UCI (Stockfish, Lc0
and so on). Engines are registered once as named setups, each with its own
strength, and are then picked like any other player: in the start menu, with
`--white` / `--black`, or mid-game.

This is project B of five (A opponent modes, B UCI engines, C profiles,
D stats page, E opening book). It builds on A's `Player` table and `Opponent`
driver.

## Decisions

| Question | Decision |
|---|---|
| Menu | Engines join the White/Black cycle; `e` opens a separate Engines screen |
| Strength | Time per move or fixed depth, plus an Elo cap when the engine offers `UCI_Elo` |
| Entries | Each entry is a named setup: the same binary can appear several times |
| Process I/O | `fork`/`exec` with non-blocking pipes, polled on the existing 100 ms tick; no new threads |

## Registry

`src/utils/engines.c`, core code with no ncurses.

The file is `$XDG_CONFIG_HOME/dchess/engines.conf`, falling back to
`~/.config/dchess/engines.conf`. It is plain text and may be edited by hand:

```ini
[Stockfish 1500]
path  = /usr/bin/stockfish
limit = time 1000
elo   = 1500

[Stockfish full]
path  = stockfish
limit = depth 20
```

- A section header is the entry name: unique, 1–40 characters, and not
  `easy`, `medium`, `hard` or `human` (case-insensitive), which the CLI and
  commands reserve.
- `path` is required. A value without a `/` is looked up on `$PATH` when the
  engine starts.
- `limit` is `time <ms>` (100–60000) or `depth <n>` (1–30). The default is
  `time 1000`.
- `elo` is optional. An entry with no `elo` line plays at full strength.
- `;` starts a comment. Blank lines are ignored.
- **Malformed input:** a bad or unknown line is skipped. A section without a
  `path` is dropped. The rest of the file still loads.
- At most 32 entries; anything beyond that is ignored on load.

```c
typedef struct {
    char name[41];
    char path[256];
    int  limit_depth;   /* 0 = limited by time instead */
    int  limit_ms;
    int  elo;           /* 0 = off */
} EngineEntry;

typedef struct { EngineEntry e[ENGINES_MAX]; int count; } EngineList;

int  engines_path(char *buf, size_t n);
int  engines_load(EngineList *l);            /* a missing file is an empty list */
int  engines_save(const EngineList *l);      /* creates the directory */
int  engines_add(EngineList *l, const EngineEntry *e, char *err, size_t n);
int  engines_remove(EngineList *l, const char *name);
const EngineEntry *engines_find(const EngineList *l, const char *name);
void engine_strength_label(const EngineEntry *e, char *buf, size_t n);
```

`engine_strength_label` gives `1s/move`, `0.5s/move`, `depth 12` or
`1s · 1500 Elo`.

## Player model

- `PlayerKind` gains `PLAYER_UCI`.
- `Player` gains `char engine[41]`, the entry name. It is a name rather than
  an index, so editing the registry cannot silently swap which engine plays.
- `player_label` and `players_pgn_name` return the entry name.
- The start menu adds the strength label after the name:
  `Stockfish 1500 · 1s · 1500 Elo`.
- `players_automated` already treats every non-human as automated, so undo,
  auto-play, the 500 ms delay and pause apply unchanged.
- `players_stats_entry` still records only games against the built-in engine.
  Project C adds per-opponent records.

## Driver interface change

`opponent_start(Opponent *o, const Position *pos, U64 key)` becomes
`opponent_start(Opponent *o, const GameState *g)`, with the key taken from
`game_hash(g)` inside. A UCI engine needs the move history to see repetitions.
The built-in driver uses `g->pos`.

`const char *opponent_error(const Opponent *o)` is new. It returns NULL, or a
message once the driver has failed. The built-in driver never fails.

## UCI driver

`Opponent *opponent_uci(const EngineEntry *e)` lives in `src/game/uci.c`. The
driver copies the entry, so later registry edits do not affect a game in
progress.

### Process

- The engine starts on the first `opponent_start`, not when the entry is
  created or picked.
- It is launched with `fork` and `execvp`, using `O_CLOEXEC` pipes for its
  stdin and stdout. Its stderr goes to `/dev/null`, so it cannot draw over
  the screen.
- The engine's stdout is read non-blocking, with lines assembled in a buffer.
  A line longer than 4 KiB is truncated.
- dchess ignores `SIGPIPE`, set once when the first UCI driver is created. A
  write to a dead engine is then an error the driver reports, not a crash.

### Protocol

Everything below is advanced by `opponent_poll`, so nothing blocks the UI.
`opponent_start` returns 1 at once; if the handshake is still running, the
search is sent as soon as it completes.

1. **Handshake:** send `uci` and wait for `uciok`, reading `id name` and
   whether `option name UCI_Elo` is offered, with its `min` and `max`.
2. **Elo:** if the entry has an `elo` and the engine offers `UCI_Elo`, send
   `setoption name UCI_LimitStrength value true` and
   `setoption name UCI_Elo value <elo>`, with the Elo clamped to the engine's
   range. If the engine does not offer it, the Elo is ignored.
3. **Ready:** send `isready` and wait for `readyok`.
4. **Search:**
   - Send `ucinewgame` before the first search of a game. A game counts as new
     when the move list is shorter than at the previous search, or its start
     position differs.
   - Send `position startpos moves …`, or `position fen <start_fen> moves …`
     when the game started from a FEN. The moves come from the game log in
     long algebraic notation (`e2e4`, `e7e8q`).
   - Send `go movetime <ms>` or `go depth <n>`.
5. **While searching:** each `info` line updates the latest depth, score,
   nodes and nps.
   - `score cp X` is kept as is.
   - `score mate M` becomes ±(`MATE_SCORE` − |M|).
   - Both are from the side to move's view, as `search()` reports.
6. **Result:** `bestmove <m>` ends the search. The move is matched against the
   legal moves of the position searched. The `SearchResult` carries that move,
   the latest score, depth, nodes, and the elapsed time.

### Stop, cancel, free

- **`opponent_stop`:** sends `stop`, then waits up to 2 s for `bestmove`. The
  next poll returns that move.
- **`opponent_cancel`:** does the same, but discards the move.
- **`opponent_free`:** sends `quit`, waits up to 0.5 s, then `SIGKILL`, then
  `waitpid`. No zombie process is left.
- If an engine ignores `stop` for 2 s, it is killed and the driver fails with
  `…: did not stop`.

### Failures

A failed driver reports a result with no move from `opponent_poll`, and
`opponent_error` returns its message:

| Failure | Message |
|---|---|
| Cannot start (exec failed, or exited during the handshake) | `<name>: could not start <path>` |
| No `uciok` or `readyok` within 10 s | `<name>: no reply from engine` |
| Exits while searching | `<name>: engine exited` |
| Illegal or unparseable best move, including `bestmove (none)` in a position with legal moves | `<name>: played illegal move <m>` |
| Ignores `stop` | `<name>: did not stop` |

The next `opponent_start` after a failure clears the error and launches the
engine again.

### Parsers

These are pure functions, unit-tested on their own:

```c
typedef struct { int depth, score_cp, is_mate, mate_in; long nodes, nps; int has_score; } UciInfo;
int  uci_parse_info(const char *line, UciInfo *out);           /* 1 if an info line */
int  uci_parse_bestmove(const char *line, char *move, size_t n); /* 1 if bestmove; "(none)" -> "" */
int  uci_parse_option(const char *line, char *name, size_t n, int *min, int *max);
void uci_position_command(const GameState *g, char *buf, size_t n);
```

## TUI

`apply_engine_result` changes for a result with no move. If the side to
move's driver reports an error, the status line shows that message, the game
pauses, and the engine panel shows `failed`. Otherwise it behaves as it does
today.

### Start menu

- Each player row cycles `You` → `dchess Easy` → `dchess Medium` →
  `dchess Hard` → every registry entry, in file order.
- An entry is labelled with its name and strength, clipped to the row.
- The hint line gains `e engines`. The Engines screen opens over the menu;
  closing it reloads the registry and rebuilds the cycle.
- If a side's entry no longer exists, that side becomes `You`.

### Engines screen

`src/tui/engines_tui.c` is a popup in the menu's style.

- **The list:** one row per entry with its name, strength and path. The path
  is clipped with `…`. A final `+ Add engine…` row follows the entries.
- **Keys:**
  - `a`, or Enter on `+ Add engine…`, adds an entry.
  - Enter on an entry edits it.
  - `t` tests the highlighted entry.
  - `d` deletes it, after a `y` to confirm.
  - Esc goes back to the menu.
- **Adding:**
  1. Type the path, or a bare name.
  2. dchess probes it: it starts the engine, sends `uci` and waits for
     `uciok`, with a "Testing…" line and a 5 s timeout. It then quits the
     engine. A failure shows the error and returns to the path prompt.
  3. Confirm the name. It defaults to the engine's `id name`, is edited
     inline, and must be unique and valid.
  4. Pick the limit. ←/→ switches between Time and Depth, and ↑/↓ changes the
     value:
     - time moves through 0.1, 0.2, 0.5, 1, 2, 3, 5, 10, 30 and 60 s;
     - depth moves through 1–30.
  5. If the probe found `UCI_Elo`, pick the Elo with ←/→: `off`, or the range
     in steps of 100.
  6. Enter saves.
- **Editing** steps through the same fields, starting from the current values.
  A changed path is probed again.
- **Test** probes the entry and shows `✓ <id name> · by <author>`, plus
  `· Elo <min>–<max>` when offered, or the error.
- Every change is written to `engines.conf` straight away.

The probe is `uci_probe(const char *path, UciProbe *out, char *err, size_t n)`
in `src/game/uci.c`. It is blocking with a timeout, which is acceptable in a
modal screen, and is unit-tested against the fake engine.

### Command line

- `--white` and `--black` also accept an entry name, e.g.
  `dchess --white "Stockfish 1500" --black hard`. Names are matched exactly,
  after the reserved words.
- An unknown name is an error that lists the registered names.
- `--engines` prints the registry (name, strength, path) and exits.
- The registry is read only when `--white` / `--black` get a value that is
  not `human`, `easy`, `medium` or `hard`, or when `--engines` is given. A
  broken `engines.conf` therefore cannot stop an ordinary start.

### In-game commands

- `white engine <easy|medium|hard|name>` and the same for black. Everything
  after `engine ` is the argument, so names may contain spaces.
- `engines` shows the registered names on the status line.
- `swap`, `pause` / `resume`, `go`, `stop` and `undo` work with UCI players as
  they do for the built-in engine.
- `depth N` changes only built-in players. A UCI entry's limit is changed on
  the Engines screen.

`players_apply_command` takes the registry as a new, last parameter, which is
NULL in the existing tests, to resolve names.

### Display

- **Engine panel:** titled with the entry name. Depth, nodes and nps come from
  the engine's `info` lines.
- **Status line:** `Stockfish 1500: Nf3 (eval +0.31, depth 18)`.
- **Eval graph:** uses the engine's score, in White's view as for dchess.

## PGN

The White and Black tags use the entry name.

## Testing

### Fake engine

`tests/fake_uci.c` is built to `build/fake_uci` before the suites run. It is
not itself a suite. Its mode is its first argument:

| Mode | Behaviour |
|---|---|
| `normal` | answers the handshake; on `go` prints two `info` lines, then plays the first legal move in the position it was given |
| `elo` | as `normal`, but advertises `UCI_Elo` (1320–3190) and appends every `setoption` line to the file named by `$FAKE_UCI_LOG` |
| `slow` | answers the handshake; on `go` plays nothing until `stop` |
| `deaf` | answers the handshake; ignores `stop` |
| `crash` | exits during `go` |
| `illegal` | answers `bestmove e2e5` |
| `mute` | never sends `uciok` |

### Unit tests

- **`tests/test_uci.c`:**
  - Parsers: `info` with a cp score, a mate score, missing fields and extra
    tokens (`pv`, `seldepth`, `hashfull`); `bestmove` with and without
    `ponder`, with a promotion, and `(none)`; `option` lines with and without
    a range.
  - `uci_position_command` for the standard start, a FEN start, and a game
    with moves.
  - The driver against each fake mode:
    - `normal` returns a legal move with depth and score;
    - `elo` sends the clamped options;
    - `stop` on `slow` returns within 2 s;
    - `deaf` is killed within about 2 s and reports an error;
    - `crash`, `illegal` and `mute` each report their message.
  - `opponent_free` leaves no child process (checked with `waitpid(-1, …,
    WNOHANG)`).
  - `uci_probe` reads the name and Elo range.
- **`tests/test_engines.c`:**
  - Save and load round trip through a temporary `XDG_CONFIG_HOME`.
  - Duplicate, reserved and over-long names are refused.
  - The 32-entry cap.
  - A malformed file loads its good sections.
  - A missing file is an empty list.
  - Strength labels.
- **`tests/test_players.c`:** UCI labels, PGN names, and
  `white engine <name with spaces>` through a registry.
- **`tests/test_cli.c`:** `--white <entry>`, an unknown entry, a reserved word
  still meaning the built-in engine, and `--engines`.
- **`tests/test_opponent.c`:** updated for `opponent_start(o, game)`.
- **Existing suites and perft** still pass.

### tmux runs

- Adding `build/fake_uci normal` through the Engines screen, then editing and
  deleting it.
- You against the fake engine, and the fake engine against dchess with
  auto-play, pause, `stop` and `undo`.
- `build/fake_uci crash`: the error on the status line, the game paused, and
  switching that side to another player.
- Deleting an entry that is selected in the menu.
- Quitting while a UCI engine is searching. `pgrep fake_uci` must find
  nothing afterwards.

## Out of scope

- Arbitrary engine options (Threads, Hash, SyzygyPath, …).
- Pondering.
- Time controls or clocks for engines.
- Tournaments and match series.
- Windows process handling.
- Downloading or installing engines.
