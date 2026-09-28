# Puzzles

**Status:** approved design, 2026-09-28
**Branch:** `feat/puzzles`

## Goal

A puzzles mode: find the best move in a position on the board, with every
move judged at once, and a rating and streak kept per profile. It works
offline.

## Data

- **Source:** the Lichess puzzle database (CC0). Fields used: `PuzzleId`,
  `FEN`, `Moves` (UCI), `Rating`, `RatingDeviation`, `Popularity`,
  `NbPlays`, `Themes`.
- **Compiled in:** `tools/mkpuzzles.c` (`make puzzles CSV=…`) reads the
  decompressed CSV and writes `src/game/puzzles_data.c`, which is
  committed. Players never need the CSV.
- **Selection:**
  - puzzles with Popularity ≥ 90, NbPlays ≥ 1000, RatingDeviation < 80,
    and at most 8 plies of moves;
  - about 3000 in total, spread evenly over 100-point rating buckets from
    600 to 2800;
  - within each bucket, puzzles with a theme the bucket lacks come first;
  - every kept puzzle is checked with dchess's own move generator (the FEN
    loads and every move is legal), and failures are skipped.
- **Themes kept** (a bit mask): mateIn1, mateIn2, mateIn3, mate (4+),
  fork, pin, skewer, discoveredAttack, hangingPiece, sacrifice, endgame,
  promotion, backRankMate, defensiveMove.
- **Record:** `{ id[8], fen, moves, rating, themes }`, in a static array
  sorted by rating.
- **Attribution:** `docs/sources.md` names the Lichess puzzle database
  (CC0).

## Rules: `game/puzzles.c`, no ncurses

- **Setup:** load the FEN, then play the first move, which is the
  opponent's. The solver plays the side to move after it.
- **Judging:**
  - a solver's move is right if it equals the next solution move, or if it
    gives checkmate;
  - after a right move that isn't the last, the opponent's reply plays
    automatically;
  - the puzzle is solved once the solver's last move is right;
  - a wrong move fails the puzzle. The position goes back to before that
    move, and the solver may keep trying, but it stays failed.
- **Hint:** the from-square of the next solution move. **Show:** the rest
  of the solution plays out. Both count as a failure.
- **API:** `puzzle_start`, `puzzle_try(move) → RIGHT | WRONG | SOLVED`,
  `puzzle_hint`, `puzzle_show_step`, all over a `GameState`, so the board
  renderer and move input work unchanged.

## Rating and storage

- **File:** `~/.local/share/dchess/puzzles/<profile>.txt` (under
  `$XDG_DATA_HOME` if set). A text file with lines of `key value`:
  `rating`, `played`, `streak`, `best_streak`, `rush_best`,
  `seen <ids…>`, `missed <ids…>`. It is written atomically (a tmp file,
  then rename).
- **Rating:** starts at 1500. Elo against the puzzle's rating, with
  K = 40 for the first 20 rated puzzles and 20 after, and a floor of 400.
- **Guests:** play everything, but nothing is saved.
- **Renames and deletes:** a profile rename renames its puzzles file too,
  and deleting a profile deletes it.

## Modes: `tui/puzzles_tui.c`

- **Entry:** `z` in the launcher opens the puzzles menu, as does
  `dchess --puzzles` (with `--profile`). Choices: Rated · Themes · Rush ·
  Missed (n).
- **Rated:**
  - the next puzzle is unseen, with the rating nearest the solver's,
    chosen at random from those within ±100 of it;
  - if none is left there, the window widens by 100 each time;
  - once every puzzle has been seen, the seen list is cleared;
  - the result updates the rating, `played`, the streak and the missed
    list.
- **Themes:** pick a theme, then puzzles of that theme near the solver's
  rating. Unrated, and `r` retries.
- **Rush:**
  - 3 minutes, starting at 800, with each solve moving the target rating
    up 100;
  - three mistakes or the clock end the run;
  - a hint or show ends the run;
  - the score is the number solved, and `rush_best` is kept.
- **Missed:** the missed puzzles, oldest first. Unrated, and a solve
  removes one from the list.

## Screen

- **Board:** the game's board renderer and move input: cursor, or typed
  moves (SAN or UCI).
- **Panel:**
  - the mode;
  - "White to move — find the best move";
  - your rating (with the change after each puzzle) and streak, or in
    Rush the score, strikes and clock;
  - after the puzzle, its rating and themes, and the Lichess id.
- **Keys:** `h` hint, `s` show, `n` next, `r` retry, `esc` back to the
  menu. The status line lists them.
- **Feedback:** a right move is tinted green, a wrong one red and taken
  back, and a solved puzzle gets a "Solved" banner.
- **Small terminals:** the same minimum size as a game (34×20).

## Tests

- **Data:** every bundled puzzle's FEN loads and every move is legal, and
  the last move of each mateIn puzzle gives mate.
- **Rules:**
  - right, wrong and solved;
  - a different mating move is accepted;
  - hint and show count as failures;
  - the opponent's reply is played.
- **Rating:** the Elo arithmetic, the K switch at 20 and the floor.
  Selection: near the rating, unseen, the window widening, and the seen
  list reset once exhausted.
- **Storage:** a round trip, a missing file (defaults), a corrupt line
  (skipped), and the rename and delete follow the profile.
- **Screen:** a tmux smoke test of the menu, a solve and a fail.

## Out of scope

Puzzles from your own games, a daily puzzle, and online sync.
