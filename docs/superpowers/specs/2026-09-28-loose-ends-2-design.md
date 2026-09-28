# Loose ends, round 2

**Status:** approved design, 2026-09-28
**Branch:** `fix/loose-ends-2`

The deferred minors from the tuning (#19) and puzzles (#20) reviews, plus a
promotion picker.

## Promotion picker

- **When it asks:** a cursor move of a pawn to the last rank. The command
  bar reads "Promote to: Q R B N (⏎ queen)".
- **The answer:** `q`/`r`/`b`/`n` (either case) chooses; ⏎ is a queen; Esc
  cancels the move and keeps the selection cleared.
- **Where:** in `tui.c` where the cursor chooses a move (`choose_move`), so
  games and puzzles both get it.
- **Typed moves:** unchanged (`e8=N`, `e7e8n`).

## Puzzles

- **Rush feedback:** after a solve or a wrong move, the verdict (the tint
  and the message) shows for 700 ms before the next puzzle.
- **Esc in Rush:** Esc mid-run ends it through the normal end of a run,
  so a new best is kept.
- **Damaged ratings:** a loaded `rating` is clamped to 400–3500.
- **Names that can't be saved:** a profile whose name `file_for` refuses
  (`/`, a leading `.`) shows "(not saved)" in the menu, as a guest does.
- **The theme menu:** it scrolls to keep the selection visible when the
  terminal is short.
- **`replay.h`:** `replay_find_san`'s declaration moves out from under
  `replay_read`'s comment.

## Tuning tools

- **`tune --terms`:** a comma list of exact names (`pesto`, `pawns`,
  `mob`, `king`, `xtra`). An unknown name is an error (exit 2).
- **genfens:**
  - it counts games that end before they start (opening or setup failed)
    as dropped, and reports played / dropped / failed;
  - it exits 1 when no game produced positions;
  - `make genfens` fails if any job fails;
  - an engine with no answer within 10 s ends that game as failed;
  - with `GAMES < JOBS`, each job plays at least one game.
- **The tuner:**
  - every weight stays within ±500;
  - `realloc` and `pthread_create` failures are checked (a failed thread
    is run inline);
  - `tune_read_params` refuses a file with extra numbers;
  - `tune_core.h`'s comment describes `tune_set_terms` correctly.
- **`match`:** a note on stderr when `--base-params` or `--cand-params`
  overrides `tuned`/`-tuned`.
- **The search:** the rule against table cutoffs on the principal line
  applies only with PVS on. Without PVS every node has a full window, and
  the table would never cut.

## Tests

- **Puzzles:**
  - `puzzle_count <= PUZZLE_MAX`, and every line has an even length;
  - a mate on a non-final move gives `PZ_SOLVED`;
  - Rush's best is kept on Esc;
  - a rating is clamped on load;
  - an unsavable name is detected.
- **Tuning:**
  - `tune_load` never puts one game in both sets;
  - `--terms` parsing: exact names, and unknown names refused;
  - `tune_read_params` refuses extra numbers;
  - weights stay within bounds.
- **genfens:**
  - with a fake engine that fails or hangs, the game is counted and the
    run exits 1;
  - `GAMES < JOBS` runs games.
- **The evaluation:** the tuned mobility term scores a mobile rook above a
  trapped one.
- **The promotion picker:** a tmux smoke test underpromotes by cursor, in a
  game and in a puzzle.
