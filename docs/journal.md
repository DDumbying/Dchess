# Dchess — Project Journal

*What this is, where it came from, what's changed, and where it's going.*

This document is the long-form companion to `README.md`. The README tells you
how to build and play. This tells the story: what the project looked like
when this work started, what changed and why, what the codebase looks like
today, and what a realistic path forward looks like. It's meant to be read
once for context and then revisited as a changelog/roadmap whenever the
project picks back up.

---

## 1. What Dchess is

Dchess is a terminal chess engine written in C, played through a full-screen
`ncurses` TUI. It's a hobby/learning project in the classic sense: a
bitboard board representation, hand-rolled move generation, a negamax
search with alpha-beta pruning, and a from-scratch terminal UI — the kind of
project people build specifically to learn how chess engines and terminal
UIs work under the hood, not to compete with Stockfish.

At a glance, it currently offers:

- Full legal move generation (castling, en passant, promotion, check
  detection) on a 12-bitboard position representation
- Negamax + alpha-beta search with quiescence search and a transposition
  table (both added during this round of work — see below)
- Material + piece-square-table evaluation
- A polished `ncurses` TUI: mouse-free vim-style modal input, move
  highlighting, an eval bar, clocks, and an interactive onboarding screen
- FEN import/export, so any position (not just the standard start) can be
  loaded, saved, or pasted in mid-game
- A persistent, versioned stats file tracking games played, win rate by
  color and difficulty, and game history
- CLI flags for scripted/non-interactive use, alongside the visual menu

Version string as of this writing: `1.0.0-alpha` (see `headers/utils/stats.h`).

---

## 2. Where it started

Before this round of work, the project was structurally solid but had a
handful of real bugs and several unfinished corners — the normal state of a
hobby project built in bursts. Specifically, when this work began:

- **Move generation had a real bug.** En-passant captures were missing the
  `FLAG_CAPTURE` flag, so `make_move()` never actually removed the captured
  pawn from the board. The capturing pawn moved correctly; a ghost pawn was
  left behind. This is the kind of bug that's easy to never notice in casual
  play (en passant is rare, and the visual board still looked plausible) but
  is a genuine correctness defect.
- **No move-generation tests existed to catch it.** `tests/perft.c` and
  `tests/test_movegen.c` were both present but completely empty (0 bytes) —
  scaffolding for tests that were never written.
- **The search had no quiescence search.** `alpha_beta()` called
  `evaluate()` the instant it hit depth 0, even mid-capture-sequence — the
  classic "horizon effect" that makes an engine walk into bad trades on the
  last ply of its search.
- **`hash.c` / `hash.h` were completely empty**, despite the README's
  description implying Zobrist-style position hashing lived in the engine.
  The actual hash function existed, but as two separately-typed, drifting
  copies pasted directly into `tui.c` and `commands.c`.
- **No transposition table**, so identical positions reached by different
  move orders (common in chess, especially early game) were re-searched
  from scratch every time.
- **The search ran synchronously on the UI thread.** At "hard" difficulty
  (depth 8), the whole terminal froze — no "thinking" indicator, no
  responsiveness — until the search finished.
- **No FEN parser existed at all.** There was no way to start from, load,
  save, or even display any position other than the standard start.
- **Only CLI flags for setup** — no interactive way to choose your side,
  difficulty, or position without knowing the flag names up front.
- **A handful of dead code paths**: an unused `castling_rights[]` array in
  `make.c` duplicating the real rights table, an `undo_move()` function that
  silently ignored its own `move` parameter and just did a full `memcpy`
  (implying an abandoned attempt at real incremental unmake), unused
  magic-bitboard scaffolding (`bishop_masks`, `rook_masks`, a `ray()`
  helper) left over in `bitboard.c`, and a first-draft knight-attack
  computation that was computed and then immediately discarded.
- **A sentinel mismatch**: `board.c` initialized "no en passant square" as
  `-1`, while `movegen.c` and `make.c` both checked against `NO_SQ` (64).
  Harmless by luck on this hardware/compiler (`1ULL << -1` happened to
  behave like `1ULL << 63`, which never matched a real pawn's attack
  pattern from the standard starting position) — but genuinely undefined
  behavior, and a real bug waiting for the right position to trigger it.
- **`stats.c` ignored `fread()`'s return value** throughout, so a
  truncated or corrupted stats file could leave the in-memory stats struct
  partially zeroed and partially garbage instead of falling back cleanly.
- `docs/overview.md` (this file, previously) was a placeholder containing
  the word "Stuff".

None of this is a knock on the project — it's exactly what an actively
worked-on hobby engine looks like mid-stream. The point of writing it down
here is so the starting point is on the record, not just the end state.

---

## 3. What changed, and why

Work happened in two rounds. The first focused on engine correctness,
search strength, and test coverage. The second added FEN support and the
onboarding screen. Both are covered in the order they actually happened,
including the parts that didn't go smoothly, because that's part of the
honest record too.

### Round 1 — correctness, search strength, and cleanup

**Quiescence search** (`src/engine/search.c`). `alpha_beta()` no longer
calls `evaluate()` directly at depth 0. Instead it hands off to a new
`quiescence()` function that keeps searching captures, promotions, and (if
the side to move is in check) all legal replies, until the position is
quiet. This is the single highest-leverage change for playing strength: it
directly fixes the horizon-effect blunders the old search was prone to.

**Hash consolidation + transposition table** (`src/engine/hash.c`,
`headers/engine/hash.h`, `src/engine/search.c`). The previously-empty
`hash.c`/`hash.h` now hold one real `hash_position()`, used by `tui.c`,
`commands.c`, and the search's new transposition table. The TT is a simple
always-replace table (~1M entries) storing exact/alpha/beta bounds and a
best move, used both for cutoffs and for move ordering.

**A forced-repaint hook instead of full threading** (`tui.c`, `commands.c`,
`headers/tui/tui.h`). The honest version of this decision: a fully
threaded, cancelable search would be the right long-term fix for the UI
freezing at high difficulty, but it touches the main input loop's control
flow in ways that needed real interactive testing — and this sandbox has no
`ncurses` development headers, so `ncurses`-dependent code could never be
compiled or run here, only read carefully. Rather than ship an untested
threading change to the main loop, a smaller, verifiable fix went in
instead: `TUIState` gained a `request_redraw` callback that `engine_move()`
calls right before the blocking `search()` call, so "Engine thinking..."
actually gets painted to the screen instead of the terminal looking frozen.
Real backgrounding of the search is still open — see the roadmap.

**Real perft and move-generation tests** (`tests/perft.c`,
`tests/test_movegen.c`, `tests/test_common.h`). Both files went from empty
to real test suites: `perft.c` checks node counts at multiple depths
against three known-correct positions (the standard start, the "Kiwipete"
torture-test position, and a promotion-heavy position), and
`test_movegen.c` adds targeted unit tests for checkmate/stalemate
detection, en passant, promotion, and castling-through-check. Writing these
immediately found the en-passant `FLAG_CAPTURE` bug described above —
exactly the kind of bug this class of test exists to catch. (Runner-up
finding, worth being upfront about: chasing that bug down included a real
detour where a stale compiled binary from earlier in the debugging session
briefly looked like a second, unrelated failure. It wasn't — once every
binary involved was rebuilt fresh from a clean checkout, the picture was
unambiguous. Recorded here mainly as a note for future work: always rebuild
from a clean tree before trusting a "the bug disappeared" result.)

**Dead code cleanup** (`make.c`, `make.h`, `bitboard.c`, `board.c`,
`stats.c`, `Makefile`). Removed the unused `castling_rights[]` array, the
misleading `undo_move()` stub (documented in `make.h` why there's no real
incremental unmake yet, rather than leaving a function whose signature
implied one existed), the abandoned magic-bitboard scaffolding in
`bitboard.c`, and the discarded first-draft knight-attack computation.
Fixed the `-1`/`NO_SQ` en-passant sentinel mismatch in `board.c`. Made
`stats_load()` check every `fread()`'s return value and fall back to a
clean zeroed state on any short/corrupt read instead of silently
continuing with partially-populated data. Removed `-Wno-unused-function`
from the `Makefile`, since it had been quietly masking exactly this class
of issue.

### Round 2 — FEN support and visual onboarding

**FEN parsing** (`src/engine/fen.c`, `headers/engine/fen.h`). `parse_fen()`
and `position_to_fen()`, tested for round-tripping (parse -> generate
reproduces the input), cross-checked independently against the
mailbox-built Kiwipete position already used by the perft suite, and tested
against seven categories of malformed input to confirm they're rejected
cleanly (leaving the destination position untouched, not partially
overwritten).

**FEN wired into the CLI and TUI.** `--fen "<string>"` as a CLI flag
(validated at parse time), `fen` as an in-game command (prints the current
position), and `loadfen <string>` as an in-game command (loads any
position mid-session -- useful for puzzles or picking up an analysis from
elsewhere). Adding this surfaced two things that needed fixing alongside
it: the "does the engine move first" check was hardcoded to
`engine_side == WHITE`, which is only correct for the standard starting
position -- a custom FEN where it's Black to move and the engine plays
Black needed a real check for "is it currently the engine's turn"; and the
in-game command-input buffer was 64 bytes, too small for a real FEN string
plus a command prefix like `loadfen `, so it's now 256.

**Interactive onboarding screen** (`src/tui/onboard.c`,
`headers/tui/onboard.h`). Running `dchess` with no arguments now shows a
keyboard-navigable menu (side / difficulty / starting position -- standard
or a pasted FEN) instead of just starting immediately. Passing any
gameplay-affecting flag (`--color`, `--difficulty`, `--two-player`,
`--fen`) skips the menu and behaves exactly as before, so existing scripts
and muscle-memory invocations are unaffected. `--menu`/`-m` forces the
screen even with other flags present (which pre-fill it); `--no-menu`
forces it off even with zero flags, restoring the old instant-start
behavior on demand. Same caveat as the redraw hook above: this is
`ncurses` UI code that could be read carefully but not compiled or run in
this sandbox, so it's been reviewed line-by-line rather than tested
end-to-end -- treat it as needing a real local test pass before relying on
it.

**Known small gap, left as-is rather than silently hidden:** combining
`--menu` with `--fen` correctly pre-fills side and difficulty in the menu,
but not the FEN itself -- the "Position" row still defaults to "Standard",
and you'd need to reselect "Custom" and re-paste the FEN. Fixable (the menu
would need the raw FEN string threaded through, which `TUIState` doesn't
currently retain past the point `tui_init()` turns it into a `Position`),
just not done yet.

---

## 4. Current architecture

```
Dchess/
├── src/
│   ├── engine/          — the actual chess engine, no ncurses dependency
│   │   ├── board.c      — Position struct, clear/init helpers
│   │   ├── move.c       — move encoding/decoding, move_to_str()
│   │   ├── movegen.c    — pseudo-legal move generation + legality via
│   │   │                  is_in_check()/is_attacked()
│   │   ├── make.c       — make_move() (with legality check baked in)
│   │   ├── eval.c       — material + piece-square-table evaluation
│   │   ├── search.c     — negamax + alpha-beta + quiescence + TT
│   │   ├── hash.c       — hash_position(), shared by search + TUI
│   │   └── fen.c        — parse_fen() / position_to_fen()
│   ├── tui/             — everything ncurses-dependent
│   │   ├── tui.c        — main loop, window layout, game-over popup,
│   │   │                  the forced-repaint hook
│   │   ├── onboard.c    — the interactive "new game" menu
│   │   ├── commands.c   — in-game command bar (moves, go, new, fen, ...)
│   │   ├── input.c      — vim-style modal keyboard input
│   │   ├── render.c     — board/info/eval-bar rendering, color setup
│   │   └── stats_tui.c  — full-screen and popup stats views
│   ├── utils/
│   │   ├── bitboard.c   — attack tables, classical (non-magic) sliders
│   │   ├── cli.c        — CLI flag parsing, --help text
│   │   └── stats.c      — versioned binary stats file read/write
│   └── main.c           — entry point: parse flags, init, hand off to tui_run()
├── headers/             — mirrors src/, one header per .c file (mostly)
├── tests/
│   ├── perft.c          — node-count correctness vs. known-good positions
│   ├── test_movegen.c   — targeted unit tests (checkmate, en passant, ...)
│   ├── test_fen.c       — FEN round-trip + malformed-input tests
│   └── test_common.h    — shared mailbox-to-Position test helper
├── docs/
│   ├── overview.md      — this file
│   └── sources.md       — personal reference/reading list
├── assets/              — screenshots/gifs for the README
└── Makefile
```

A few things worth knowing about this layout if you're picking the project
back up:

- **`engine/` has zero `ncurses` dependency.** Everything under `src/engine`
  and `src/utils` compiles and links standalone (that's exactly how the
  test suite builds them). Only `src/tui/*.c` and `src/main.c` need
  `ncurses`.
- **There's no shared header for the TUI's color-pair IDs.** `render.c`
  defines the canonical list; `tui.c` and `onboard.c` each redefine the
  handful of `#define CP_*` constants they personally use, matching values.
  It works because the values are stable, but it's fragile -- a genuine
  candidate for a small `tui/colors.h` extraction (see roadmap).
- **`include/engine.h` is a leftover empty file** (0 bytes) not referenced
  from anywhere. `build/` is an empty directory. Both are harmless but
  worth deleting in a future pass, and a compiled `dchess` binary is
  currently checked into the repo root -- probably wants a `.gitignore`
  entry rather than being tracked.
- **The transposition table and quiescence search are engine-only changes**
  -- nothing in `tui/` needed to change for either.

---

## 5. What's still genuinely open

Being direct about the current gaps, in the order they'd probably matter
most if you sat down to keep improving this:

1. **Search is still O(depth) full-position copies, not incremental
   make/unmake.** Every node does a full `Position` struct copy before
   trying a move and restores it after. Works, is simple, is not free.
2. **No iterative deepening or time budget.** "Hard" means "depth 8,
   however long that takes" rather than "search until N seconds have
   passed." Combined with the TT this is now nearly free to add, since each
   shallow iteration seeds move ordering for the next.
3. **The UI-freeze fix is a repaint hook, not real threading.** It solves
   "the screen looks dead" but not "you can't interact while the engine
   thinks" or "you can cancel a slow search."
4. **Same-value eval regardless of game phase.** The king uses one
   piece-square table for both middlegame and endgame, so the engine won't
   naturally "activate" the king in king-and-pawn endgames the way a
   tapered/phase-aware eval would.
5. **The onboarding screen and redraw hook are code-reviewed, not
   execution-tested**, for the reason stated above (no `ncurses` dev
   headers in this environment). This is the most important item to
   verify locally before relying on either.
6. **`--menu` + `--fen` together don't fully compose** (see §3).
7. **No opening book, no PGN import/export, no UCI protocol support** -- so
   Dchess can't currently interoperate with other chess GUIs, engines, or
   game databases; it's a closed, self-contained TUI experience.

---

## 6. What Dchess should be

Stepping back from the punch list, here's a candid answer to "what would
finishing this actually look like":

**As an engine:** correct (perft-verified across more than three positions,
ideally to depth 6 on all of them, plus a couple of specifically
check/pin/promotion-heavy edge-case positions), reasonably strong for its
size class (quiescence + TT + iterative deepening + a phase-aware eval gets
a hobby engine surprisingly far), and fast enough that "hard" mode responds
in a predictable, bounded time rather than an unpredictable one.

**As a TUI:** responsive at every difficulty level (real backgrounding,
with the option to interrupt or extend a think), discoverable for a
first-time user (the onboarding screen is a step in this direction), and
transparent about what's happening (the eval bar and status line already do
a good job here -- worth preserving that character rather than over-adding
chrome).

**As a project:** something a person could pick up, read the engine/ code
in an afternoon, and understand every decision -- which is really the whole
point of a project like this. Test coverage that actually protects that
goal (a perft failure should be nearly impossible to ship unnoticed, the
way it very nearly was here). A README and this document that stay
accurate as things change, rather than drifting from the code the way
`docs/overview.md` had.

**Optionally, if it wants to grow past "personal learning project":** UCI
protocol support would let it plug into any standard chess GUI (Arena,
CuteChess, even lichess bot accounts) for free, without touching the TUI at
all -- the engine's search/movegen layer is already cleanly separated from
the `ncurses` layer, which makes this more tractable than it might sound.

---

## 7. Roadmap

Roughly in priority order, based on effort vs. payoff:

**Next up (small, contained, high payoff):**
- Iterative deepening + a time budget in `search()`, replacing the fixed
  `depth` parameter with "search until T milliseconds have elapsed,"
  reusing the existing TT for move-ordering across iterations.
- A tapered/phase-aware evaluation, at minimum a separate endgame king PST.
- Delete `include/engine.h` and the empty `build/` directory; add a
  `.gitignore` for the compiled `dchess` binary.
- Extract the duplicated `CP_*` color-pair `#define`s into one shared
  `headers/tui/colors.h`.

**Medium-sized, real payoff, needs local `ncurses` testing:**
- Real background threading for `search()` (a worker thread instead of the
  blocking call + repaint hook), ideally with a cancel path so switching
  difficulty or making a new move doesn't have to wait out a slow search.
- Thread the original `--fen` string through onboarding so `--menu --fen
  "..."` fully composes instead of dropping the FEN.
- Incremental make/unmake to replace the full-`Position`-copy-per-node
  approach in the search -- a genuine speed win, but touches castling
  rights, captured-piece bookkeeping, and en passant state together
  carefully enough to deserve its own dedicated pass rather than a quick
  bolt-on.

**Larger, optional, "grow the project" scale:**
- UCI protocol support, so Dchess's engine can be driven by any standard
  chess GUI instead of only its own TUI.
- PGN import/export and a basic opening book.
- A puzzle/analysis mode built on the FEN infrastructure that already
  exists (load a puzzle position, restrict input to finding the right
  move, reuse `eval`/`fen` commands for feedback).
- Expand `perft.c` with more known-hard test positions (there are a few
  more standard ones beyond Kiwipete and the promotion-heavy position used
  here) and push depths higher where runtime allows.

---

## 8. Quick reference: verifying any of this yourself

```bash
# Build the real game (needs ncursesw)
make && ./dchess

# Run the engine-only test suites (no ncurses needed)
gcc -Iheaders -Itests -O2 -Wall tests/perft.c src/engine/*.c src/utils/bitboard.c -o /tmp/perft && /tmp/perft
gcc -Iheaders -Itests -O2 -Wall tests/test_movegen.c src/engine/*.c src/utils/bitboard.c -o /tmp/tm && /tmp/tm
gcc -Iheaders -Itests -O2 -Wall tests/test_fen.c src/engine/*.c src/utils/bitboard.c -o /tmp/fen && /tmp/fen
```

All three should print "All ... passed." with no `FAIL` lines. If you add a
new engine feature, adding a matching perft/unit-test case first is the
cheapest insurance available in this codebase -- it's exactly what caught
the en-passant bug described in §3.
