# Dchess user guide

Everything dchess does, one feature at a time. The [README](../README.md) has
the short version and the full key and command lists; the
[project journal](overview.md) has how and why it was built.

- [First run and the launcher](#first-run-and-the-launcher)
- [Playing](#playing)
- [Opponents and engines](#opponents-and-engines)
- [Time controls](#time-controls)
- [The opening book](#the-opening-book)
- [Analysis and game review](#analysis-and-game-review)
- [Replays](#replays)
- [Puzzles](#puzzles)
- [Profiles, history and stats](#profiles-history-and-stats)
- [Files](#files)
- [dchess in other programs (UCI)](#dchess-in-other-programs-uci)
- [Troubleshooting](#troubleshooting)
- [Recording the demo clips](#recording-the-demo-clips)

## First run and the launcher

The first `dchess` shows a welcome: your name (it starts as your login name)
and a theme, which recolours the screen as you press ←→. Enter creates your
profile. If an older dchess left stats behind, they are imported into it.

After that, `dchess` opens the launcher:

- **Profiles** (left). ↑↓ switches the active profile; `n`, `r` and `d`
  create, rename and delete.
- **New game** (centre). Each row cycles with ←→:
  - **White / Black** — you, another profile, a guest, dchess at Easy, Medium
    or Hard, or a registered UCI engine
  - **Pos** — the standard start, or ⏎ to type a FEN
  - **Clock** — untimed, a preset, or `custom…` (see [Time controls](#time-controls))
  - **Book** — built-in, off, or your own `.bin`
  - **Hints** — the analysis engine, or off
  - **Theme** — with a live preview

  A mini-board beside the rows shows the starting position.
- **Your card** (right, on wide terminals). Your record, win rate, streak and
  latest games; Tab to it and ⏎ replays a game.

Whatever you start with is remembered for the profile. `e` opens the
engines screen, `s` the stats page, `Esc` quits.

Any gameplay flag on the command line (`--no-menu`, `--color`, `--white`,
`--fen`, …) skips the launcher; `--menu` shows it anyway.

## Playing

Two ways to move:

- **Cursor.** Move with `hjkl` or the arrows, Enter on a piece to see its
  legal moves, Enter on a target to play. `Esc` drops the piece.
- **Typing.** Press `i`, type `e2e4` (or a command) and Enter.

The side panel shows the evaluation (or analysis), the clocks, the moves with
the opening's ECO code and name, and what the engine is doing. `u` takes a
move back, Space pauses, Tab opens a stats popup.

Useful commands (all typed after `i`):

| Command | Does |
|---------|------|
| `go` | an engine plays one move for the side to move, even while paused |
| `stop` | a thinking engine plays its best move so far |
| `new`, `undo`, `flip`, `swap` | new game, take back, turn the board, exchange players |
| `white engine hard`, `black human` | change who plays a side, mid-game |
| `loadfen <FEN>`, `fen` | load a position, show the current one |
| `pgn [path]` | save the game |
| `resign`, `quit` | resign for the human side, leave |

## Opponents and engines

**dchess** plays at three levels:

| Level | Depth | Time per move |
|-------|-------|---------------|
| Easy | 2 | up to 1.5 s |
| Medium | 5 | up to 3 s |
| Hard | as deep as it gets (about 12–13) | up to 5 s |

In a timed game it budgets from its clock instead, still capped at its
level's depth.

**UCI engines** (Stockfish, Leela, Berserk, …) are added on the engines
screen (`e` in the launcher):

- `a` adds one by its path; dchess starts it and reads its name.
- Enter edits it: a time per move or a fixed depth, plus an Elo limit when
  the engine supports `UCI_Elo`.
- `t` tests it (starts it once and reports what it found), `d` deletes it.

Registered engines appear in the White and Black rows and in the Hints row,
and `dchess --engines` lists them. An engine that crashes or sends an illegal
move pauses the game with a message rather than ending it.

Engine against engine works too: `dchess --white hard --black "Stockfish"`.
Space pauses them; `go` plays one move while paused.

## Time controls

Games are untimed unless you pick a control. The launcher's Clock row cycles
`1+0`, `3+0`, `3+2`, `5+0`, `5+3`, `10+0`, `10+5`, `15+10` and `30+0`;
`custom…` then ⏎ takes your own:

- `M+S` — minutes, plus seconds added after each move: `7+2`, `0.5+0`
- `W/B` — time odds, White's then Black's: `5+0/1+0`

`--clock 3+2` sets one for a single run without changing your profile.

- The clock starts on White's first move. Under ten seconds a clock turns red
  and shows tenths.
- A side whose clock reaches zero loses on time — unless the other side has
  only a king, or a king and one bishop or knight, which is a draw.
- dchess and UCI engines manage their own time (UCI engines get the real
  `wtime`/`btime`/`winc`/`binc`).
- Space pauses and freezes both clocks, even between two people; moves and
  `go` wait for `resume`. Undo gives the time of the undone move back.

Saved games carry the standard `TimeControl` tag, and the stats page groups
results into bullet, blitz, rapid and classical.

## The opening book

dchess's engine plays opening moves from a book: a built-in set of about 140
main lines, or any Polyglot `.bin` (Book row, `--book path.bin`, or `book
path.bin`). It plays book moves until 8 plies at Easy and 16 at Medium, and
as far as the book goes at Hard. `book off` turns it off. The book never
affects UCI engines.

Whichever book is in use, the moves panel names the opening from the
built-in lines, e.g. `B90 Sicilian Defence · Najdorf`, and saved games record
it.

## Analysis and game review

`a` turns analysis on or off, in a game or a replay. The panel shows:

- a bar of White's winning chances,
- the score and depth (`+0.57 d7`, or `M3` for a forced mate),
- the best move (its squares are tinted on the board),
- the line the engine expects.

The engine is dchess unless you pick another in the Hints row or with
`analyse <name>` (`analyse builtin`, `analyse off`). dchess's engine can only
think about one thing at a time, so its analysis waits while it is playing a
move ("engine thinking"); a UCI analysis engine runs alongside.

In a replay, `r` reviews the whole game: every position is analysed in turn,
and each move is marked by how much it lost for the side that played it —
`?!` half a pawn, `?` a pawn, `??` three pawns or more (a forced mate given
away counts as `??`). The command bar shows the current move's mark and
cost, `n`/`N` jump to the next and previous marked move, and `r` again stops
the review.

## Replays

Open a game from:

- your card in the launcher (Tab, ↑↓, ⏎),
- the stats page's recent list (Tab to it, ↑↓, ⏎),
- any PGN file: `dchess --replay file.pgn`. With several games a picker
  opens first.

In a replay, ←→ step, Home/End jump, Space plays one move a second, and the
move list shows the whole game with the moves still ahead dimmed. `p` plays
on from the position shown, as a new game with your profile's usual setup.
Esc goes back to where you came from.

The reader handles PGN from other programs: comments, variations, NAGs and
annotation glyphs, `0-0` and `o-o`, `e8=Q`, `e.p.`, CRLF files, and files of
bare moves with no tags. A move it cannot read stops that game there with
"stopped at move N".

## Puzzles

`z` in the launcher, or `dchess --puzzles` (with `--profile NAME` to solve as
someone else), opens the puzzles menu:

- **Rated:** puzzles near your rating, never repeated until you have seen
  them all.
- **Themes:** one kind at a time — mate in 1, 2 or 3, longer mates, forks,
  pins, skewers, discovered attacks, hanging pieces, sacrifices, endgames,
  promotions, back-rank mates, defence. Unrated.
- **Rush:** three minutes, starting easy and getting harder with each solve.
  Three mistakes end the run. Your best score is kept.
- **Missed:** the rated puzzles you got wrong, oldest first. A clean solve
  takes one off the list.

The opponent's move is played first; you then find the best reply. A move is
right if it is the puzzle's move or any move that mates. The opponent's
answers play themselves. A wrong move is taken back and tinted red; you may
keep trying, but the puzzle counts as missed.

Keys: move by cursor (⏎ twice) or type it after `i` (`Nf3` or `g1f3`); `?`
tints the piece to move, `s` shows the solution — both count as a miss, and
end a Rush run; `n` moves on (an unfinished rated puzzle counts as a miss),
`r` tries it again (unrated), Esc goes back to the menu.

**The rating** starts at 1500 and moves like Elo against the puzzle's
Lichess rating: by up to 40 points a puzzle for your first 20, then 20. It
never drops below 400. Once a puzzle is over, the panel shows its rating,
themes and its Lichess page. Everything is kept per profile in
`~/.local/share/dchess/puzzles/<profile>.txt`. Renaming a profile keeps it;
deleting one deletes it.

**The set** is about 3200 puzzles from the
[Lichess puzzle database](https://database.lichess.org/#puzzles) (CC0),
built into dchess. `make puzzles CSV=lichess_db_puzzle.csv` rebuilds it from
the full CSV: popular, often-played puzzles with settled ratings, 140 per
100 points from 600 to 2899, every theme represented, each checked with
dchess's own move generator.

## Profiles, history and stats

Each profile keeps its own remembered setup (players, clock, book, analysis
engine, theme) and its own history. Every finished game a profile plays is
appended to `games.pgn`.

The stats page (`s` in the launcher, `dchess --stats`, or `stats` in a game)
shows for one profile at a time (←→ switches):

- games, wins, draws and losses, and win rate as White and as Black
- the trend, current and best streak, and games per week
- opponents with their record against you
- how games ended, and results by time control
- the recent games, which open as replays

`dchess --profiles` prints every profile's record; `--profile NAME` plays as
another profile for one run.

## Files

| File | What |
|------|------|
| `~/.config/dchess/profiles.conf` | profiles and their remembered setup |
| `~/.config/dchess/engines.conf` | registered UCI engines |
| `~/.local/share/dchess/games.pgn` | every finished game |
| `~/.local/share/dchess/puzzles/<profile>.txt` | a profile's puzzle rating and history |
| `~/.local/share/dchess/stats.dat` | stats from before profiles, imported once |

The config files follow `XDG_CONFIG_HOME` when it is set. All of them are
plain text you can edit (with dchess closed).

`profiles.conf`:

```ini
active = saeed

[saeed]
theme    = gruvbox
white    = saeed
black    = hard
book     = builtin
analysis = builtin
clock    = 5+3
```

`engines.conf`:

```ini
[Stockfish 17]
path  = /usr/bin/stockfish
limit = time 1000
elo   = 1500
```

`games.pgn` is standard PGN, readable by any chess program. Besides the
usual tags, dchess adds `WhiteKind`/`BlackKind` (profile, guest, dchess,
engine), the engine strength, `EndReason`, `PlyCount`, `Seconds`,
`TimeControl` and the opening.

## dchess in other programs (UCI)

dchess is also a UCI engine. In a chess GUI (Arena, Cute Chess,
BanksiaGUI, …) add it as an engine with the path to the `dchess` binary —
started through pipes with no arguments it speaks UCI on its own; `dchess
--uci` does the same explicitly. It answers `uci`, `isready`,
`ucinewgame`, `position`, `go` (`wtime`/`btime`/`winc`/`binc`, `movetime`,
`depth`, `infinite`), `stop` and `quit`, reports `info depth … score … pv
…` as it searches, and has one option, `OwnBook` (the built-in opening
book, on by default).

It can also play inside dchess: register the `dchess` binary on the engines
screen and pick it for a side.

`make match ARGS="--vs /usr/bin/stockfish --vs-elo 2300"` measures dchess
against another engine at a set strength — see the journal for the
results.

The evaluation's weights are tuned from games, not set by hand:
`make genfens` has Stockfish play itself (5000 games, about half an hour on
14 cores) and keeps the quiet positions with each game's result;
`make tune ARGS="--terms pesto,king,mob"` then fits the weights of those
terms to the results and writes `build/tuned_params.h`, plus a `.txt` copy
for `make match ARGS="--cand all --cand-params build/tuned_params.h.txt"`.
If the match says the new weights do not lose, copy the header over
`src/engine/tuned_params.h`.

## Troubleshooting

- **The pieces look wrong or misaligned.** The board uses the Unicode chess
  symbols; use a font that has them (most do), and a UTF-8 locale.
- **"Terminal too small".** The game needs at least 34×20; the launcher's
  full dashboard wants 100 columns.
- **A UCI engine does nothing.** Use Test on the engines screen; it shows
  whether the engine started and answered. The path must be the engine
  binary itself.
- **Colours are off.** dchess uses 256 colours when the terminal offers them
  and falls back to eight; try another theme.

## Recording the demo clips

The README's clips are recorded, not drawn: `tools/demo/make-demos.sh`
rebuilds all of them into `assets/gifs` (it needs tmux, asciinema 3 and
`agg`). Each clip is a scene file in `tools/demo` that drives dchess key by
key in a scratch home, so re-running it after a UI change keeps the README
honest.
