# Better evaluation and a UCI engine mode

**Status:** approved design, 2026-09-28
**Branch:** `feat/eval-uci`

## Goal

Two things that go together:
- **A stronger evaluation.** Each term is measured and kept only if it does
  not lose.
- **UCI engine mode.** `dchess --uci` lets dchess play in any GUI, and lets
  it be measured against Stockfish at known strengths.

## A. Evaluation (`engine/eval.c`)

```c
typedef struct { int pesto, pawns, mobility, king, extras; } EvalOptions;
EvalOptions eval_default_options(void);
void eval_set_options(const EvalOptions *o);   /* before a search, like SearchOptions */
```

Every term produces a middlegame and an endgame score. The two are blended
by game phase (the existing 0..24 phase). With every option off, the result
equals today's `evaluate()` exactly.

**The terms:**
1. **pesto.** Tapered material and piece-square tables for all six pieces,
   using PeSTO's published values (Rofchade), fetched from the Chess
   Programming Wiki. They replace today's tables.
2. **pawns.** Penalties for doubled and isolated pawns, and a bonus for a
   passed pawn (no enemy pawn ahead on its own or adjacent files) that grows
   with rank, weighted to the endgame.
3. **mobility.** For each knight, bishop, rook and queen, the attacked
   squares not holding its own pieces, times a per-piece weight (middlegame
   and endgame).
4. **king.** Middlegame only:
   - a bonus for each own pawn directly in front of the king or on the two
     files beside it (the shield), within two ranks;
   - a penalty for each file beside the king (or the king's own) with no
     own pawn.
5. **extras.** The bishop pair, and a rook on an open or half-open file.

**Keeping a term:** each term is added on its own and kept if `make match`
(the previous set against the new one, 80 games at 50 ms) scores at least
50% within the error. The results go in the ledger and the journal.

**Tests** (`tests/test_eval.c`):
- **Symmetry:** a position and its colour-flipped mirror (ranks mirrored,
  colours swapped, side to move swapped) evaluate the same, over 12 varied
  positions, with every option on.
- **One hand-checked position per term:**
  - a doubled pawn scores below the same pawn undoubled;
  - a passed pawn on the 6th scores above one on the 3rd;
  - a knight in the centre is more mobile than one in the corner;
  - a shielded king scores above an exposed one;
  - the bishop pair scores above a bishop and knight.
- **Options off:** everything off equals today's evaluation on the bench
  positions.
- The existing suites pass.

## B. UCI engine mode

**Module:** `game/uci_engine.c`, with
`int uci_engine_run(FILE *in, FILE *out)`. `main.c` runs it for `--uci`
before any ncurses setup.

**Commands:**

| Command | Reply |
|---------|-------|
| `uci` | `id name dchess <version>`, `id author`, `option name OwnBook type check default true`, `uciok` |
| `isready` | `readyok` |
| `setoption name OwnBook value true\|false` | — |
| `ucinewgame` | clears the search |
| `position startpos\|fen <FEN> [moves …]` | builds the game via `GameState` |
| `stop` | the running search returns its best move |
| `quit` | ends the loop |
| unknown | ignored |

**`go`:**
- `depth N`: the search at that depth, no time limit.
- `movetime T`: T minus 20 ms.
- `wtime`/`btime`/`winc`/`binc`: `tc_budget_ms` of the side to move's time.
- `infinite`, or nothing: until `stop`.
- With `OwnBook` on and a book move available, the book move is played at
  once.

**Threading:** the search runs in a thread. The reading loop stays
responsive, so `stop` cancels through `search_cancel()`, and `isready`
answers during a search.

**Output:**
- After each completed depth:
  `info depth D score cp S|mate M nodes N time T pv …`.
  - This uses a new `search_set_info(void (*fn)(const SearchInfo *))`
    callback, called from the search thread.
  - The score is from the side to move, and a mate is given in moves.
- Then `bestmove <move>`.
- Output is flushed after every line.

**Tests** (`tests/test_uci_engine.c`), through `fmemopen`/pipes:
- the handshake gives `uciok` and `readyok`;
- `position startpos moves e2e4 e7e5` then `go depth 3` gives a legal
  `bestmove` and at least one `info depth`;
- a FEN position with a mate in 1 gives `bestmove` with the mating move and
  `score mate 1`;
- `go infinite`, then `stop` 200 ms later, gives `bestmove` within 1 s;
- `quit` returns 0.

**Live check:** the engines screen registers `dchess --uci` and plays it
against the built-in engine.

## C. Measuring against other engines

`tools/match.c` gains:
- `--vs PATH`: the opponent is an external UCI engine, driven through
  `game/uci.c` (`opponent_uci`) with `limit = time ms`;
- `--vs-elo N`: sets `UCI_Elo`.

The candidate is the in-process dchess with its default options. The report
is the same (score and Elo difference). Against Stockfish 18 at a known
`UCI_Elo`, dchess's rating is roughly that Elo plus the difference.

**Measurements** (ledger, journal and PR):
- before and after the evaluation work, against Stockfish at 1320 and 1600
  (or whatever is near 50%);
- 60 games each at 100 ms.

## Out of scope

- UCI options beyond `OwnBook` (Hash, Threads, MultiPV, ponder).
- Tuning the evaluation's weights automatically.
- Endgame tablebases.
