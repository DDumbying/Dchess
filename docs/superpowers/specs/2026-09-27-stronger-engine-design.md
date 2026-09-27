# Stronger engine

**Status:** approved design, 2026-09-27
**Branch:** `feat/stronger-engine` (from `main` after #16)

## Goal

A stronger built-in search, measured rather than assumed. Hard gets the
whole gain (its depth cap is lifted; it keeps its 5 s budget). Easy (depth 2)
and Medium (depth 5) keep their feel. Analysis and timed games already use
the full depth, so they gain too.

## Measuring: `tools/match.c` (`make match`)

- **Players:** one program plays games between two `SearchOptions` sets,
  *baseline* and *candidate*.
- **Openings:** 40 openings, each the built-in book line cut at ply 8.
  Every opening is played twice, with colours swapped.
- **Per move:**
  - the side to move's options are set;
  - `search_clear()` runs;
  - `search(pos, MAX_DEPTH, ms)`, with a fixed time per move (default 50;
    `--ms N`).
- **Game end:** the game uses `GameState`, so mate, stalemate, repetition,
  the fifty-move rule and insufficient material end it; 200 plies is a draw.
- **Output:** W-D-L for the candidate, score %, and Elo difference ± 95%
  error from the score (logistic).
- **Usage:**
  - `--games N` (default 80, rounded up to an even number);
  - `--base LIST` / `--cand LIST` switch features by name (`pvs,asp,nmp,lmr,ext,tt`),
    where `all` or `none` is the starting point and `-name` removes a
    feature;
  - progress goes to stderr and the summary to stdout.

## Search changes (`engine/search.c`)

```c
typedef struct { int pvs, aspiration, null_move, lmr, check_ext, tt_depth; } SearchOptions;
void search_set_options(const SearchOptions *o);   /* defaults: all on */
SearchOptions search_default_options(void);
```

**The techniques:**
- **PVS.** The first move at a node is searched with the full window; the
  others with a null window `(alpha, alpha+1)`, re-searched with the full
  window on a fail-high inside `(alpha, beta)`.
- **Aspiration.** From depth 4, the root searches `score ± 50`. A fail-low
  or fail-high widens that side to ±200, then to the full window.
- **Null move.** Used when:
  - `depth >= 3`;
  - the side to move is not in check;
  - it has a piece other than pawns and the king;
  - the static evaluation is at least `beta`;
  - and the previous move was not a null move.

  The side to move is flipped (and the en-passant square cleared), and the
  position is searched at `depth - 1 - R` with `R = 2 + (depth > 6)`, using
  the window `(-beta, -beta+1)`. A result of at least `beta` returns
  `beta`.
- **LMR.** For quiet moves:
  - from the 4th move on, at `depth >= 3`;
  - not in check;
  - the move gives no check;
  - it is not a killer, not a capture and not a promotion.

  The reduction is 1, or 2 from the 8th move at `depth >= 6`. A reduced
  score above `alpha` is re-searched at full depth.
- **Check extension.** A node in check searches one ply deeper, capped so
  that `ply < MAX_DEPTH - 1`.
- **TT depth.** Replace an entry when:
  - the key differs and the entry is from an older search (`age`); or
  - the new depth is at least the old one; or
  - the new entry is EXACT.

  Mate scores are stored relative to the node (`score ± ply`) and restored
  on probe; the "never cache mates" rule goes.
- **Aborted searches** store nothing, as now.

**Levels:** Hard's depth becomes `MAX_DEPTH` (`cli.c` difficulty table).
Easy and Medium are unchanged. The Hard time budget (5 s) stays.

**Keeping a technique:** each one is added on its own and kept only if a
match against the previous step scores at least 50% within the error. The
final result against `main`'s search is reported in the PR and in the
journal.

## Testing

**`tests/test_search.c`:**
- **Mates:** mate-in-1, mate-in-2 and mate-in-3 positions are found with the
  right distance (`MATE_SCORE - plies`).
- **Zugzwang:** with every option on, the king-and-pawn ending
  `8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1` finds `e1f1`.
- **Tactics:** 20 Win At Chess positions at 1 s each (the list is in the
  test) solve at least as many as the baseline, whose count is recorded in
  the test.
- **Options:** `search_set_options` with everything off gives the same best
  move as today's search at depth 5 on the bench positions.

The existing suites (perft, movegen, analysis, opponent, …) pass unchanged.
`make bench` reports fewer nodes at depth 6.

**Matches**, recorded in the ledger and the PR: each technique against the
previous step, then the whole set against `none`.

## Out of scope

- Evaluation changes (tapering every piece).
- Multi-threaded search.
- A UCI engine mode.
- Incremental make/unmake.
