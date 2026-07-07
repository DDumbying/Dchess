# Dchess - DumbChess
<img src="./assets/images/dchess.png" align="left" width="120" hspace="10" vspace="10">

**A terminal chess engine written in C.**

One of these nerdy things built out of passion — to actually understand how `C` works and how chess works technically, under the hood.

**Links:** [GitHub](https://github.com/ddumbying/) · ~[Documentation](https://ddumbying.vercel.app/projects/dchess/)~ (*WIP*) </br>

## Overview

<p align="center">
  <img src="assets/gifs/demo.gif" width="100%"/>
</p>

## What it has

**Engine**
- Bitboard-based board representation
- Full move generation — pawns, castling, en passant, promotion
- Iterative deepening alpha-beta search (depth 1, 2, 3, ... up to the
  difficulty's cap or its time budget, whichever comes first) with a
  transposition table and move ordering (TT move, then captures/promotions)
- Quiescence search — keeps resolving captures/promotions/check-evasions
  past the nominal depth so the engine doesn't misjudge a position mid-trade
- Static evaluation with material values (centipawns) and piece-square tables
  for all piece types, with a phase-tapered king PST (encourages castling and
  king safety in the middlegame, centralization in the endgame)
- 50-move rule detection
- Threefold repetition detection via position hashing
- Stalemate and checkmate detection

**TUI**
- ncurses interface with Unicode chess pieces (♙♘♗♖♕♔ / ♟♞♝♜♛♚)
- Board scales to fill available terminal size
- Four built-in color themes (classic/midnight/forest/contrast), switchable
  from the onboarding screen (live preview), the CLI, or an in-game command
- Interactive onboarding screen — pick side, difficulty, starting position,
  and theme visually; CLI flags remain available as a scriptable alternative
- FEN import/export — start from, view, or load any position, not just the
  standard setup
- The engine thinks in a background thread — the clock, redraws, and
  `quit` all keep working while it's calculating, even at "hard" difficulty
- Cancellable search — `stop` takes the engine's current best guess
  immediately; starting a new game or loading a position while it's
  thinking cancels the stale search automatically instead of waiting
- Vim-style modal input — normal mode for cursor navigation (`hjkl`/arrows), press `i` to enter command mode, `ESC` to return
- Legal move highlighting — blue squares for valid destinations
- Selected piece highlighted in green
- Check highlighted on the board — red square, gold king
- Last-move tint on from/to squares
- Move history, captured pieces, material advantage displayed in the side panel
- Live per-turn clock for both sides — starts counting on the first move, not at launch
- Evaluation bar updates live after every engine response
- Game-over popup appears immediately on checkmate/stalemate without needing a keypress
- Engine plays one side, human the other — configurable at launch or mid-game
- **Two-player local mode** — no engine, board flips 180° after each move so the next player faces their own pieces

**Statistics**
- Persistent stats saved to `~/.local/share/dchess/stats.dat`
- Win/loss/draw breakdown by difficulty (easy / medium / hard)
- Performance by color — games played and wins as white vs. black
- Overall record with a stacked W/L/D bar
- Avg moves per game, longest game, avg time per game, total play time
- Rolling win-rate history graph — plots win rate and loss rate over time using a sliding 10-game window, with date labels and a 50% guide line. Keeps the last 256 games.
- Two stat views:
  - **Tab** (in-game overlay) — small centered popup with W/L/D bar, win rate, per-difficulty breakdown and avg time; dismisses on any key
  - **Full stats screen** — all sections plus the history graph filling the remaining space; accessible via `dchess --stats` or `st` command in-game

## CLI

```
USAGE
  dchess [OPTIONS]

OPTIONS
  -c, --color <white|black>       Choose your side (default: white)
  -d, --difficulty <easy|medium|hard>
        easy   – depth 2, up to 1.5s  (quick, forgiving)
        medium – depth 5, up to 3s   (balanced)  [default]
        hard   – depth 8, up to 5s   (challenging, slower)
  -2, --two-player                Local two-player mode — no engine, board flips after each move
  --fen <string>                  Start from a custom FEN position instead of the standard setup
  -m, --menu                      Show the onboarding screen to pick options visually,
                                   even if other flags were given
  --no-menu                       Skip onboarding and start immediately (classic instant-start)
  --theme <name>                  Color theme: classic | midnight | forest | contrast
                                   (default: classic)
  -s, --stats                     Show statistics in a full TUI screen and exit
  -V, --version                   Print version and exit
  -h, --help                      Show help and exit

EXAMPLES
  dchess                          Onboarding screen (pick side/difficulty/position visually)
  dchess --no-menu                Start immediately with defaults (white, medium)
  dchess --color black            Play as black, no menu
  dchess --difficulty hard        Hard mode, no menu
  dchess -c black -d easy         Black side, easy difficulty, no menu
  dchess --fen "<FEN string>"     Start from a custom position
  dchess --menu -d hard           Onboarding screen, pre-filled to hard difficulty
  dchess --theme midnight         Start with the midnight color theme
  dchess --two-player             Local two-player, board flips each turn
  dchess --stats                  View your stats
```

Running `dchess` with no arguments shows an interactive onboarding
screen (side / difficulty / starting position / theme) so you don't need
to remember flags. Pressing `s` from that screen shows your stats; `ESC`
quits dchess entirely rather than starting a game. Passing any gameplay
flag (`--color`, `--difficulty`, `--two-player`, `--fen`, `--theme`) skips
it and starts immediately, so scripts and muscle-memory invocations keep
working exactly as before.

## Build

```bash
make
./dchess
```

Requires `ncursesw`.

## Controls

**Cursor — normal mode (default)**
```
h / ←       move cursor left
l / →       move cursor right
k / ↑       move cursor up
j / ↓       move cursor down
Enter       select piece / confirm move
Esc         deselect
i           enter command/insert mode
Tab         open in-game stats popup (any key to close)
```

**Command mode** (press `i` to enter, `ESC` to exit)
```
e2e4        make a move in algebraic notation
go          let the engine play the current side
stop        have a thinking engine return its best move now, instead
            of waiting out the rest of its time budget
new         reset the board
flip        swap which side the engine plays
depth N     change search depth (1–8) mid-game
eval        show current position evaluation
fen         show the current position as a FEN string
loadfen <FEN>
            load a custom position mid-game
theme <name>
            switch color theme: classic | midnight | forest | contrast
st          open the full stats screen
help        list in-game commands
quit / q    exit
```

## Scope

This engine isn't trying to be perfect — it's one of those things built because having a chess engine is **cool** and because it teaches you things.

So it's limited by design, and currently doesn't have:
- No networking
- No GUI (no plans either)
- ~~Could have its own design~~ — it does now
- F Windows
- IDK what else, we'll see
