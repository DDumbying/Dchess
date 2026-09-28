# Changelog

All notable changes to dchess. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The [project journal](docs/overview.md)
has the how and why.

## [1.0.0] - 2026-09-28

### Play
- A launcher: profiles, a new-game setup on one screen, and your record,
  with a first-run welcome.
- Any mix of players on each side: you, a guest, dchess at Easy, Medium or
  Hard, or any registered UCI engine with its own strength and Elo limit.
- Time controls: presets from 1+0 to 30+0, custom `M+S`, White/Black odds,
  losing on time, engines that budget their own clock, and a pause.
- Opening books: built-in main lines or any Polyglot `.bin`, with the
  opening's ECO code and name as you play.
- Promotion by cursor asks which piece; any starting position by FEN.

### Analysis
- An eval bar, the score and depth, the best move tinted on the board, and
  the expected line, from dchess or a UCI engine.
- Game review: every move graded, with inaccuracies, mistakes and blunders
  marked.
- Replays of saved games and any PGN file, with play-on from any position.

### Puzzles
- About 3200 Lichess puzzles (CC0), built in: rated, by theme, a
  three-minute Rush, and the ones you missed.
- A puzzle rating, streaks and best Rush per profile.

### Engine
- Principal-variation search with aspiration windows, null-move pruning,
  late-move reductions, check extensions and a transposition table; Hard
  searches as deep as its time allows.
- A tapered evaluation: PeSTO tables, king safety, and mobility with
  weights tuned from Stockfish games.
- A UCI engine mode (`--uci`, or started through pipes): about 2450–2550
  against a strength-limited Stockfish at 100 ms a move.

### Profiles and history
- Every game saved to a standard PGN file, with tags for players,
  strength, time control, opening and ending.
- A statistics page: record, win rates, trend, streaks, opponents, endings
  and results by time control.

### Tools
- `make install` / `make uninstall` and a man page.
- `make match` (Elo between search options, or against any UCI engine),
  `make genfens` and `make tune` (Texel tuning), and `make puzzles`.
- CI runs the whole test suite on every pull request.

## [1.0.0-alpha] - 2026-05-03

The first tagged build: the engine, the board, and the first profiles and
stats. See rounds 1–7 of the journal.
