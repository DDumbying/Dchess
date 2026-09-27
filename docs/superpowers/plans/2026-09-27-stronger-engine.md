# Stronger Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A measurably stronger built-in search, with the gain going to Hard, analysis and timed games.

**Architecture:**
- Each technique is a switch in `SearchOptions`.
- `tools/match.c` plays baseline against candidate from book openings and reports Elo ± error.
- `tests/test_search.c` pins mates, zugzwang and a tactics count.

**Tech Stack:** C, `make test`, `make match`, `make bench`.

**Spec:** `docs/superpowers/specs/2026-09-27-stronger-engine-design.md`

> Terse by request. The spec holds the technique details.

## Global Constraints

- Zero warnings. The engine stays free of ncurses. Sparse comments. No attribution trailers.
- Perft and every existing suite must pass after each task.
- A technique is kept only if its match scores at least 50% within the error; otherwise it is ruled out or tuned, and the result is recorded in the ledger.
- The search stays single-instance (globals); the match tool plays its games in sequence.

## Review Focus

1. **Null move in zugzwang and in pawn-only endings:** no wrong result, since the pieces-only rule holds. Test in Task 3.
2. **LMR re-search correctness:** a reduced move that beats alpha is searched again at full depth, so no tactic is missed. Tactics count in Task 4.
3. **Aspiration windows with mate scores and fail-low loops:** they always terminate, and mate distances stay exact. Mate tests in Task 2.
4. **TT mate-score adjustment:** mate distances are right across transpositions, and the "M" count in analysis is correct. Test in Task 5.
5. **The time limit inside re-searches and null moves:** an aborted search never returns a partial iteration's move. The existing opponent and analysis tests, plus a timed match, in each task.

---

### Task 1: Options, the match tool, the baseline tests

**Files:**
- Modify `headers/engine/search.h` and `src/engine/search.c` (the options struct; the features do nothing yet).
- Create `tools/match.c`.
- Modify `Makefile` (`match` target).
- Create `tests/test_search.c`.

- [ ] **Step 1:** Write `test_search.c`:
  - mate in 1/2/3 at depth 6, checking the best move and `best_score == MATE_SCORE - plies` for a mate-in-N-moves (plies = 2N−1);
  - 20 Win At Chess positions at 1000 ms each, printing the solved count;
  - the zugzwang position with its expected move (verify it with the options off; if the position does not behave, pick a known zugzwang test and ledger it).
- [ ] **Step 2:** Add `SearchOptions`, `search_set_options` and `search_default_options`, with no effect yet. Record the baseline tactics count as the test's floor.
- [ ] **Step 3: `match.c`.**
  - Openings: the first 40 built-in lines (via `game/openings.h`), cut at ply 8.
  - Colour-swapped pairs; `GameState` ends games; 200 plies is a draw.
  - Options parsing; Elo ± error; stderr progress.
- [ ] **Step 4:** `make match` against itself (`--base none --cand none --games 40 --ms 30`) → about 50% with a wide error. Commit `feat(search): switchable search options and a match tool`.

### Task 2: PVS and aspiration windows

- [ ] **Step 1:** Implement PVS behind `o.pvs`.
- [ ] **Step 2:** Run `make test`, then the match `--base none --cand pvs --games 80 --ms 50` → ledger.
- [ ] **Step 3:** Implement aspiration behind `o.aspiration`. Run `make test` (the mate tests exercise the aspiration edges), then the match `--base pvs --cand pvs,asp` → ledger.
- [ ] **Step 4:** Commit `feat(search): principal-variation search and aspiration windows`.

### Task 3: Null-move pruning

- [ ] **Step 1:** Implement it per the spec. The zugzwang and mate tests must stay green.
- [ ] **Step 2:** Match `--base pvs,asp --cand pvs,asp,nmp` → ledger.
- [ ] **Step 3:** Commit `feat(search): null-move pruning`.

### Task 4: Late-move reductions and check extensions

- [ ] **Step 1:** Implement LMR, then the tactics count and a match → ledger.
- [ ] **Step 2:** Implement the check extension, then a match → ledger.
- [ ] **Step 3:** Commit `feat(search): late-move reductions and check extensions`.

### Task 5: The transposition table

- [ ] **Step 1: Test.** Mate scores are correct when the transposition table is warm: search the mate-in-3 twice without `search_clear` and get the same distance.
- [ ] **Step 2:** Implement the replacement scheme, an age counter per `search()`, and ply-relative mate storage.
- [ ] **Step 3:** Run `make test`, then a match → ledger. Commit `feat(search): depth-preferred table with mate scores`.

### Task 6: Levels, the final measurement, docs

- [ ] **Step 1:** Hard's depth becomes `MAX_DEPTH` in the `cli.c` table. Update the help text and README (`hard – deepest search in 5 s`).
- [ ] **Step 2:** Final match `--base none --cand all --games 200 --ms 50`, plus `make bench` before and after → ledger and journal (round 9 notes, and the open items updated).
- [ ] **Step 3:** Run `make -B test`. Commit `feat(engine): Hard searches as deep as its time allows`.
