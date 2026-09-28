# Tuning the evaluation's weights

**Status:** approved design, 2026-09-28
**Branch:** `feat/tuning`

## Goal

Replace the hand-set weights of the added evaluation terms with weights
learned from Stockfish self-play games (Texel tuning), then re-measure each
term. The PeSTO tables stay fixed.

## Parameters

- **Where they live:** `engine/eval.c`'s added-term weights move into one
  table, `EvalParams` (`headers/engine/eval.h`), instead of literals.
- **The weights (all ints):**
  - doubled pawn (mg, eg);
  - isolated pawn (mg, eg);
  - passed pawn by relative rank 1–6 (mg, eg);
  - mobility weight and typical count for each of N, B, R, Q (mg, eg
    weights);
  - king shield pawn, and king open file (mg);
  - bishop pair (mg, eg);
  - rook on an open and on a half-open file (mg, eg).
- **Access:** `eval_params()` returns the live table and `eval_set_params()`
  replaces it.
- **Values:** the defaults are today's literals, so behaviour is unchanged
  until tuned values are written. Tuned values live in
  `src/engine/tuned_params.h`, included by `eval.c`.

## Data: `tools/genfens.c` (`make genfens`)

- **Games:**
  - Stockfish against itself through the existing UCI client, at
    `movetime 10` and full strength;
  - from a random built-in book line cut at 4–8 plies, plus 2 random legal
    moves;
  - adjudicated by `GameState` (mate, draws), with 300 plies counting as a
    draw.
- **Positions recorded:** from ply 10 on, one line `FEN;result` per position,
  where the result is 1, 0.5 or 0 from White's point of view. A position is
  recorded only when the side to move is not in check and the move that
  reached it was not a capture or a promotion.
- **Usage:**
  - `--games N --seed S --out FILE`;
  - `make genfens` runs 14 processes with different seeds and concatenates
    their output;
  - the target is 5,000 games and at least 300,000 positions.

## Tuner: `tools/tune.c` (`make tune`)

- **Loading:** the positions are loaded and every 10th is held out as the
  validation set.
- **Error:** `E = mean((result − σ(eval_white))²)`, with
  `σ(s) = 1 / (1 + 10^(−K·s/400))`, where `eval_white` is `evaluate()` from
  White's view with every term on.
- **Scale:** K is fitted first, by golden-section search on [0.1, 3] with
  the current parameters.
- **Local search:**
  - each parameter is tried at +1, then −1, and a change is kept when the
    training error drops;
  - a pass covers every parameter;
  - it stops when a pass changes nothing, or when the validation error rises
    for 2 passes in a row (keeping the best-validation parameters).
- **Output:** progress (pass, training and validation error) goes to stderr,
  and the best parameters are written as `tuned_params.h`.
- **Threads:** the evaluation is single-threaded and only reads `pos` and
  the parameters, so the tuner splits error computation across threads
  (pthreads, `--threads N`).

## Measuring

**Self-play:** with the tuned parameters, each added term is matched against
the current defaults (PeSTO + king), 80 games at 50 ms; a term is kept if it
scores at least 50% within the error. The combined kept set is then matched
over 200 games.

**Rating:** against Stockfish @2300 and @2600, 60 games each at 100 ms, as in
round 10.

Results go in the journal (round 11) and the PR.

## Testing

**`tests/test_eval.c`:**
- the default `EvalParams` equals today's hard-coded values (evaluation
  unchanged on the bench positions with every term on);
- symmetry holds with the tuned parameters.

**`tests/test_tune.c`:**
- σ and the error on three hand-made positions match a hand computation;
- the K fit returns a value in (0.1, 3);
- one local-search step on a tiny set does not increase the training error.

## Out of scope

- Tuning the PeSTO tables.
- Gradient-based tuners.
- New evaluation terms.
