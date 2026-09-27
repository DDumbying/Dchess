# Evaluation and UCI Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A measured, stronger evaluation, and `dchess --uci`, with dchess's rating estimated against Stockfish.

**Architecture:**
- Evaluation terms sit behind `EvalOptions`, like `SearchOptions`.
- `game/uci_engine.c` runs the UCI protocol over `FILE*` streams, with the search in a thread.
- `tools/match.c --vs` plays against any UCI engine through the existing client.

**Tech Stack:** C, `make test`, `make match`, Stockfish 18.

**Spec:** `docs/superpowers/specs/2026-09-28-eval-and-uci-design.md`

> Terse by request.

## Global Constraints

- Zero warnings. The engine and game code stay free of ncurses. Sparse comments. No attribution trailers.
- With every option off, the evaluation equals today's. A term is kept only if its match scores at least 50% within the error; each result is ledgered.
- The search is single-instance. In UCI mode only one search runs at a time.

## Review Focus

1. **Evaluation symmetry** for positions with castling rights, en passant, and pawns on the edge files. Symmetry test in Task 2.
2. **`position … moves` with castling, promotion and en passant in UCI coordinates** (`e1g1`, `e7e8q`). UCI tests in Task 6.
3. **A `stop` that arrives before the search thread starts, or after it ends:** there must be exactly one `bestmove`. Test in Task 6.
4. **`go` with no time left** (`wtime 0`), or with only the opponent's clock given: a legal move within about a second. Test in Task 6.
5. **Mobility's cost in search speed:** the time-based match accounts for it; also check the bench nodes-per-second. Task 4.

---

### Task 1: `match --vs` and the baseline rating

- [ ] `--vs PATH`, `--vs-elo N` and `--vs-ms N` (the opponent's time, default the same `--ms`). The opponent is `opponent_uci` with `limit_ms` and `elo`. Games alternate colours, and each game's result uses the same `GameState` rules.
- [ ] Baseline: `--cand all --vs stockfish --vs-elo 1320 --games 60 --ms 100`; if the score is far from 50%, also 1600 → ledger.
- [ ] Commit `feat(match): measure against any UCI engine`.

### Task 2: `EvalOptions` and PeSTO

- [ ] Tests: symmetry (12 positions, all options on); options off equal to today's `evaluate()` on the bench positions (record the values first).
- [ ] Split `evaluate()` into middlegame and endgame accumulators. Add `EvalOptions` with none on, and keep today's path when `pesto` is off.
- [ ] `pesto`: fetch PeSTO's tables (WebFetch, Chess Programming Wiki "PeSTO's Evaluation Function"), mind the square order, and add a symmetry check.
- [ ] Match `--base all --cand all` is meaningless here, so compare with eval options: extend match's option list with `pesto`, `pawns`, `mob`, `king`, `xtra` applying to `EvalOptions`. Run `--base none --cand pesto` style (search options all on in both) → ledger.
- [ ] Commit `feat(eval): tapered PeSTO tables`.

### Task 3: Pawn structure

- [ ] Hand-checked tests (doubled, isolated, passed by rank) → implement → symmetry → match → ledger → commit.

### Task 4: Mobility

- [ ] Hand-checked test → implement → symmetry → match; check bench nps → ledger → commit.

### Task 5: King safety and extras

- [ ] Test, implement and match each term separately → ledger → commit.

### Task 6: UCI engine mode

- [ ] Tests in `tests/test_uci_engine.c` per the spec, plus Review Focus 2–4.
- [ ] `search_set_info` callback; `game/uci_engine.c`; `--uci` in the CLI and `main.c`; help text.
- [ ] Live check: engines screen → add `./dchess --uci` → it plays against the built-in engine (tmux).
- [ ] Commit `feat(uci): dchess as a UCI engine`.

### Task 7: The rating, and the docs

- [ ] After-rating against Stockfish (same settings as the Task 1 baseline) → ledger.
- [ ] README (engine, UCI mode, rating), guide (UCI section), journal (round 10), help → commit.
