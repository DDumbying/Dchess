# Dchess — Project Journal

*What this is, where it came from, what's changed, and where it's going.*

This document is the long-form companion to `README.md`. The README tells you
how to build and play. This tells the story: what the project looked like
when this work started, what changed and why, what the codebase looks like
today, and what a realistic path forward looks like. It's meant to be read
once for context and then revisited as a changelog/roadmap whenever the
project picks back up.

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
- Negamax + alpha-beta search with iterative deepening, quiescence search,
  and a transposition table (all added across this round of work — see below)
- Material + piece-square-table evaluation
- A polished `ncurses` TUI: mouse-free vim-style modal input, move
  highlighting, an eval bar, clocks, four color themes, and an interactive
  onboarding screen
- FEN import/export, so any position (not just the standard start) can be
  loaded, saved, or pasted in mid-game
- A persistent, versioned stats file tracking games played, win rate by
  color and difficulty, and game history
- CLI flags for scripted/non-interactive use, alongside the visual menu

Version string as of this writing: `1.0.0-alpha` (see `headers/utils/stats.h`).

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

### Round 3 — themes, onboarding fixes, and iterative deepening

**A real screenshot surfaced two onboarding bugs.** The hint text at the
bottom of the onboarding box was long enough to wrap past the window's
right edge, landing on and visually corrupting the bottom border. Fixing
it properly meant catching a second copy of the same mistake: every
labeled row's width budget had forgotten to subtract its own label
prefix's length, so the same overflow was one config change away from
happening on any row, not just the hint line. Both are now computed from
`pw` (the box's actual width) so they're arithmetically guaranteed to stop
one column short of the border. Separately, ESC used to mean "skip
onboarding, start with defaults" -- reasonable in the abstract, but not
what a person expects from ESC in a menu, and not what was reported ("it
doesn't exit, it just enters the chessboard"). ESC now quits the program
outright; the only way into a game is explicitly selecting "Start Game".

**Color themes** (`src/utils/theme.c`, `headers/utils/theme.h`). Four
built-in palettes -- `classic` (the original), `midnight`, `forest`,
`contrast` -- selectable via `--theme <name>` on the CLI, a "Theme" row in
onboarding with a live preview (the palette actually changes on screen as
you cycle left/right, since `init_colors()` is safe to call repeatedly),
or an in-game `theme <name>` command. The palette table itself is
deliberately in a plain, `ncurses`-free file, for the same reason
`engine/` stays `ncurses`-free: `cli.c` needs to validate `--theme`
without pulling `ncurses` into a file that otherwise builds and tests
standalone in a plain C environment.

**A stats shortcut inside onboarding**, bound to `s`. This one had a
near-miss worth recording: the obvious first attempt called the existing
`show_stats_overlay()`, which turned out to call `initscr()` and, at the
end, `endwin()` -- entirely reasonable for its actual use case (the
standalone `--stats` CLI flag, run before any other `ncurses` state
exists) but exactly wrong from inside an already-running session, where
that `endwin()` would have torn down the live game's terminal state.
Used the lower-level `draw_stats_overlay()` (draws into a window the
caller supplies, no `initscr`/`endwin` of its own) instead, with an
explicit `werase(stdscr); refresh();` afterward so the full-screen stats
view doesn't leave visual leftovers around the onboarding box once it's
dismissed.

**Iterative deepening + a time budget** (`src/engine/search.c`). `search()`
now takes a max depth *and* a millisecond budget, and searches depth 1,
2, 3, ... up up to whichever runs out first. Depth 1 always completes
uncompromised regardless of the budget, so a legal move is always
available even under an unreasonably tight limit. Time is checked
periodically (every 2048 nodes, not every node -- `clock_gettime()` isn't
free) from inside `alpha_beta()`/`quiescence()`; when the deadline passes,
every open frame unwinds immediately and the in-progress iteration's
result is discarded in favor of the last one that finished cleanly. Each
difficulty now pairs its existing depth cap with a time budget (easy:
1.5s, medium: 4s, hard: 12s) via `cli_time_limit_for_difficulty()`.

This turned up something worth flagging plainly: testing it revealed that
depth 8 from the *opening position* (the highest branching factor in the
game) doesn't reliably finish even within hard's full 12-second budget on
this engine, and — because the search had no time limit at all before this
change — that means "hard" mode's very first move could previously have
taken considerably longer than 12 seconds, unpredictably, with nothing to
stop it. The time budget doesn't make the search faster; it makes its
worst case bounded and predictable, which is arguably the more important
of the two.

**Follow-up correction: the initial time budgets were too generous to be
useful.** Measuring depth-by-depth from the opening position told a
sharper story than expected:

| Depth | Time | Best move | Score |
|-------|------|-----------|-------|
| 4 | 3 ms | b1c3 | 0 |
| 5 | 220 ms | b1c3 | +5 |
| 6 | 685 ms | b1c3 | 0 |
| 7 | 26,211 ms | b1c3 | 0 |
| 8 | 173,190 ms (~2.9 min) | b1c3 | 0 |

The recommended move and evaluation don't change at all from depth 4
through depth 8 here -- depth 7 and 8 cost enormously more (no null-move
pruning or late-move reductions to tame the branching factor) for zero
practical benefit on a calm position. Hard's original 12-second budget was
sized as if that extra time were buying real strength; on quiet positions
it mostly wasn't, so every single move during the opening and much of the
middlegame was silently eating the full 12 seconds for no measurable gain.
Time budgets were revised down to something that actually reflects this:
easy 1.5s (unchanged), medium 3s (was 4s), hard 5s (was 12s) -- sized to
comfortably finish depth 6 and occasionally reach into depth 7 on sharper
or simpler positions, without committing to depth 7/8's full cost on
every move regardless of whether the position calls for it. The
underlying lesson for later work (see "same-value eval" and "incremental
make/unmake" below): this engine's branching factor scales badly past
depth 6 without more advanced pruning, and no amount of time-budget tuning
fixes that -- it just avoids paying for it uselessly.

### Round 4 — real background threading for the search

This is the highest-risk change made so far: it touches the main input
loop's control flow directly and introduces genuine concurrency, in a
sandbox with no `ncurses` development headers to compile or run any of it
against. Given that, this round leaned harder on verification than any
previous one, using two techniques that hadn't been available before:

**A minimal `ncurses.h` stub, built specifically for this.** It declares
(not implements) every `ncurses` symbol the codebase actually uses --
enough for `gcc -c` to fully type-check every `.c` file in `src/tui/`
against real function signatures, catching typos, mismatched types, and
unbalanced braces the same way a real build would. With dummy function
*bodies* added on top, the entire project -- every engine file, every
util, every TUI file, `main.c` -- linked successfully against the stub
and the real `pthread` library, meaning every `pthread_create`/
`pthread_mutex_*`/`pthread_join` call resolves correctly. This doesn't
prove the UI renders correctly or behaves right on screen, but it does
conclusively rule out the class of mistake most likely in a change this
size: a typo or type mismatch code review alone might miss.

**A direct functional test of the threading logic itself**, bypassing
`ncurses` entirely by calling `handle_command()`/`poll_engine_search()`
straight from a small test program against a real `TUIState` and the
real engine. This confirmed, end to end: `"go"` returns immediately
without blocking; a conflicting command ("new") issued while the engine
is thinking is correctly rejected; the search actually completes in the
background (~800ms at depth 6, matching earlier measurements) and the
resulting move is correctly applied to the live position; a duplicate
`"go"` while already running doesn't spawn a second thread; and `"quit"`
is still honored immediately even mid-search. This is real evidence the
logic works, not just that it compiles.

**The design**, for anyone extending it later: `search()` itself needed
no changes. The worker thread searches a *private copy* of the position
(`state->search_snapshot`, captured at kickoff, along with the depth and
time-limit arguments) rather than `state->pos` directly -- so the main
thread can keep safely reading/rendering the live position the entire
time a search is running, and there's no shared mutable state for the
worker to race on beyond a small mutex-protected completion flag and
result buffer. The scope was deliberately kept narrow: while a search is
in flight, any command that would mutate the position or call `search()`
again concurrently (a move, "go", "new", "loadfen", "flip", "eval") is
rejected with a "please wait" status rather than allowed to race;
read-only or cosmetic commands (fen, theme, help, stats, quit) still work
immediately. There is deliberately no cancellation in this round --
a search always runs to completion (or its time budget) once started.
Genuine cancellation is a natural, well-scoped follow-up: `search.c`
already has the exact machinery needed (the `search_aborted` flag used
for the time budget) and would mostly need a thread-safe way for the
main thread to set it early.

One more deliberate choice worth recording: quitting while a search is
still running does not wait for it. `main()` returns immediately after
`tui_run()`, which ends the whole process (and every thread in it)
atomically -- there's nothing the search thread holds (files, locks) that
needs an orderly release, so blocking "quit" for up to the remaining time
budget would be a real regression for no benefit.

Despite all of the above, this is still the piece of this project most
in need of a real, hands-on local test before being trusted -- the stub
and the direct functional test both rule out entire categories of bug,
but neither one presses actual keys against an actual running terminal.

### Round 5 — search cancellation

The natural follow-up flagged at the end of Round 4: a running search
couldn't be interrupted early, so switching difficulty, starting a new
game, or loading a position while the engine was thinking just waited
for a "please wait" rejection. That's fixed now, reusing machinery that
already existed rather than adding much new: `search()` already discards
an in-progress iteration and falls back to the last one that completed
cleanly whenever its time budget runs out (see Round 3's `search_aborted`
flag) -- cancellation is the same code path, just triggered by a request
from outside instead of a clock.

The one real addition was making that flag safe to set from a *different*
thread, which the time-budget check never needed to be: `search_cancel()`
uses `stdatomic.h` rather than a plain `int`, specifically because it's
the one piece of state in `search.c` actually shared between the UI
thread and a running search worker.

Three new pieces of UI behavior, split by what a person actually means
when they interrupt a think:

- **`stop`** -- a new in-game command, same idea as UCI's `"stop"`: tells
  a thinking engine to return its best move *right now* rather than
  waiting out its time budget, and that move gets played. Always has a
  legal move to give back, for the same reason the time-budget path
  always does.
- **`new` / `loadfen` / `flip`** -- these mean "replace the game state
  entirely," so a stale search computing a move for a position that's
  about to stop existing isn't worth waiting for. These now cancel and
  *discard* the in-flight result (rather than apply it) and proceed
  immediately, instead of either blocking or rejecting the command.
- Moves, `"go"`, and `"eval"` still just get rejected with a "please
  wait, or use 'stop'" message while the engine thinks -- none of them
  mean "start over," so force-cancelling on their behalf didn't seem
  right.

**Verification followed the same shape as Round 4, extended to cover the
new cross-thread piece specifically**: the `ncurses` stub was rebuilt and
every TUI file re-verified against it (still clean), the whole project
re-linked against real `pthread` (still clean), and a new direct
functional test exercised `search_cancel()` from a genuinely different
thread than the one running `search()` -- confirming a cancelled depth-8,
no-time-limit search (which would otherwise run for minutes) returns a
legal move within a few hundred milliseconds of the request, and that a
fresh search afterward isn't immediately killed by a stale cancellation
flag left over from the previous one. A second functional test drove
`stop`/`new`/`flip` through `handle_command()` directly against the real
engine and confirmed each cancels cleanly, within the same tight
time window, with no stale result sneaking in afterward. Two mistakes
surfaced along the way, both in the *test* setup rather than the code
(a wrong expected side-to-move, and a test that didn't set `engine_side`
and was then confused by the correct-but-unexpected behavior that
followed) -- recorded here mainly as a reminder that test code needs the
same scrutiny as the code it's testing.

### Round 6 — tapered king evaluation, and a significant pre-existing bug it uncovered

The planned work here was narrow and contained: add a separate
`king_endgame_pst` and blend it with the existing middlegame `king_pst`
by a material-based game-phase count (the standard PeSTO-style scheme --
each minor/major piece contributes a weight, pawns and kings contribute
none, and the total tells `evaluate()` where it is between "everyone's
still on the board" and "mostly traded off"). Only the king is tapered;
every other piece keeps its single table, matching the specific gap this
was meant to close rather than becoming a full tapered-eval rewrite.

While verifying that change empirically (checking that a White king on
e1 scores better than on e4 in the middlegame, the way it obviously
should), the numbers came back backwards -- e4 scored *better*. That
led to checking a castled king (g1) against a random exposed square
(a4): the random square won. And a pawn one step from promoting scored
*worse* than one still on its starting square. All three symmetric for
Black too, meaning this wasn't a white/black asymmetry -- it was
`evaluate()` scoring king safety and pawn advancement backwards for
*both* sides equally, which doesn't cancel out in any meaningful way; it
just means whichever side the engine is playing, it's been mis-valuing
two of the most basic principles in chess since the very first version
of this evaluation function, untouched across every previous round of
this work.

The root cause: `king_pst`, `pawn_pst`, `rook_pst`, and friends are
written in the standard chess-programming convention used by every
public reference for these exact values -- row 0 represents rank 8, row
7 represents rank 1 -- which is the opposite of this engine's own native
square numbering (a1 = 0). The original code applied `mirror()` to
Black's square and indexed White's directly; it needed to be the other
way around; White is the side whose native numbering doesn't already
match the table's layout. Both sides were being evaluated through the
same wrong lookup, which is exactly why the symptom was symmetric rather
than a white-favors-black (or vice versa) skew that might have been
easier to notice by just watching games.

This was verified the way most findings in this project have been:
empirically, not by staring at the array literals and reasoning about
what "should" be correct. Small positions with the two kings alone
gave a misleading first read (a bare-king endgame *correctly* wants a
centralized king even before any fix, which looks superficially similar
to the bug and briefly caused real confusion mid-investigation -- see
`game_phase()` above; anything tested with only kings on the board has
`phase == 0` and is testing `king_endgame_pst`, not `king_pst`).
The test that actually isolated the bug added a full, mirrored,
material-neutral complement of the other pieces first, pinning the
phase to its middlegame maximum before comparing king/pawn/rook squares
-- at which point the backwards behavior (and, after the one-line fix,
the corrected behavior) was unambiguous. All of this is now a permanent
regression test in `tests/test_eval.c`, not just a one-off check.

### Round 7 — themes didn't actually change anything on many terminals

Reported directly: "the themes still doesn't work." Re-reading
`init_colors()` explained why. It has two branches -- one for terminals
that support `can_change_color()` (custom RGB values, what every theme's
palette in `theme.c` is expressed as) and an 8-color fallback for
terminals that don't. Plenty of real terminals fall into the second
category. The fallback branch was completely hardcoded -- every color
pair in it used literal `COLOR_CYAN`/`COLOR_GREEN`/`COLOR_BLUE`/etc.
regardless of which theme was active, because it predates the theme
system entirely and nothing had gone back to update it. On any terminal
using that path, switching themes was changing internal state
correctly but had zero visible effect, which matches the report exactly.

Fixed by giving `Theme` a second, small palette alongside its RGB
values -- five plain 0-7 color numbers (`fb_accent`, `fb_cursor_bg`,
`fb_sel_bg`, `fb_movehi_bg`, `fb_check_bg`) for the handful of elements
worth theming even with only 8 colors available, and wiring those into
the fallback branch in place of the hardcoded constants. Board squares
themselves deliberately stay a fixed black/white in every theme even in
fallback mode -- colored square backgrounds risk hurting piece
readability more than theming is worth, so only the accent/highlight
colors vary here, same as the full-color path leaves the actual board
squares theme-colored but still readable.

`classic`'s fallback accent was deliberately kept identical to the
previous hardcoded value (cyan), so this fix doesn't change anything
about the default look -- it only makes the other three themes
distinguishable from it and from each other on terminals without custom
color support. Verified directly (portable, no `ncurses` needed, since
`theme.c` has none): every theme's fallback accent is confirmed distinct
from every other theme's, and `classic`'s is confirmed unchanged.

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
│   │   ├── eval.c       — material + piece-square-table evaluation,
│   │   │                  tapered king PST by game phase
│   │   ├── search.c     — negamax + alpha-beta + quiescence + TT
│   │   ├── hash.c       — hash_position(), shared by search + TUI
│   │   └── fen.c        — parse_fen() / position_to_fen()
│   ├── tui/             — everything ncurses-dependent
│   │   ├── tui.c        — main loop, window layout, game-over popup,
│   │   │                  polls poll_engine_search() once per iteration
│   │   ├── onboard.c    — the interactive "new game" menu
│   │   ├── commands.c   — in-game command bar (moves, go, new, fen, ...);
│   │   │                  also the background engine-search thread
│   │   ├── input.c      — vim-style modal keyboard input
│   │   ├── render.c     — board/info/eval-bar rendering, color setup
│   │   └── stats_tui.c  — full-screen and popup stats views
│   ├── utils/
│   │   ├── bitboard.c   — attack tables, classical (non-magic) sliders
│   │   ├── cli.c        — CLI flag parsing, --help text
│   │   ├── stats.c      — versioned binary stats file read/write
│   │   └── theme.c      — color theme table (no ncurses dependency)
│   └── main.c           — entry point: parse flags, init, hand off to tui_run()
├── headers/             — mirrors src/, one header per .c file (mostly)
├── tests/
│   ├── perft.c          — node-count correctness vs. known-good positions
│   ├── test_movegen.c   — targeted unit tests (checkmate, en passant, ...)
│   ├── test_fen.c       — FEN round-trip + malformed-input tests
│   ├── test_eval.c      — PST orientation + king-tapering regression guard
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

## 5. What's still genuinely open

Being direct about the current gaps, in the order they'd probably matter
most if you sat down to keep improving this:

1. **Search is still O(depth) full-position copies, not incremental
   make/unmake.** Every node does a full `Position` struct copy before
   trying a move and restores it after. Works, is simple, is not free.
2. **Cancellation (Round 5) covers `stop`/`new`/`loadfen`/`flip`, but
   moves, `"go"`, and `"eval"` still just reject with "please wait"
   while the engine thinks**, rather than doing anything smarter. That's
   a deliberate choice (none of them mean "start over," so
   force-cancelling on their behalf didn't seem right) rather than a gap,
   but it's worth listing since it means the engine still can't be
   casually interrupted mid-think just to eval a different line, say.
3. **Only the king is phase-tapered (Round 6); every other piece still
   uses one table regardless of game phase.** Knights, bishops, rooks,
   and pawns don't get worse for this (their PSTs are far less
   phase-sensitive than the king's to begin with), but a fuller
   PeSTO-style tapered eval would extend the same idea to all of them,
   not just the king.
4. **The onboarding screen, theming, and stats-overlay integration are
   verified by a syntax-checking `ncurses` stub and (for the threading
   logic specifically) a direct functional test, but never against a
   real running terminal.** The stub and the functional test (Round 4)
   are both genuinely more rigorous than code review alone -- the stub
   catches typos/type errors across the whole `ncurses` layer, and the
   functional test proved the actual threading behavior end to end -- but
   neither one presses a key against a real screen. This is still the
   most important thing to verify locally before trusting any of it, and
   Round 3 exists precisely because the first real execution test (a
   screenshot) immediately found two bugs that review alone had missed.
5. **`--menu` + `--fen` together don't fully compose** (see §3).
6. **No opening book, no PGN import/export, no UCI protocol support** -- so
   Dchess can't currently interoperate with other chess GUIs, engines, or
   game databases; it's a closed, self-contained TUI experience.
7. **Depth 7-8 essentially never completes from the opening position within
   hard difficulty's 5-second budget** (see Round 3) -- measured at ~26s
   and ~170s respectively from the starting position, against ~0.7s for
   depth 6. Not a bug -- the time budget is deliberately sized around this
   measurement rather than pretending the extra depth is affordable -- but
   worth knowing that "hard" mostly plays at an effective depth 6-ish
   rather than 8 in practice, especially early in the game.
8. **No null-move pruning, late-move reductions, or other techniques that
   tame branching factor at higher depths.** This is the real reason #7
   exists -- the jump from depth 6 to depth 7 costs roughly 38x the time
   for (on quiet positions) no change in the recommended move. Adding
   even basic null-move pruning would likely let "hard" reach a genuinely
   deeper, more meaningfully stronger search within the same time budget,
   rather than just hitting the same wall a little later.

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

## 7. Roadmap

Roughly in priority order, based on effort vs. payoff:

**Right now, before anything else:** a real local test pass. Round 3
exists because a single screenshot immediately found two bugs (the
overflow, the ESC behavior) that careful code review alone had missed
across two prior rounds. Onboarding navigation, the theme live-preview,
`s` for stats from inside onboarding, ESC-quits, and a game at "hard"
difficulty (to see the time-budget behavior) are the highest-value things
left to actually click through.

**Next up (small, contained, high payoff):**
- Null-move pruning and/or late-move reductions in `alpha_beta()`. This is
  now backed by a real measurement (see Round 3): depth 6→7 costs roughly
  38x the time for no change in the recommended move on a quiet position.
  Taming that jump is likely the single highest-leverage change left for
  actual playing strength -- more valuable than raising the depth cap or
  the time budget, since right now both just hit the same wall a little
  later rather than searching meaningfully deeper.
- Extend tapering (Round 6 added it for the king specifically) to the
  rest of the pieces for a fuller PeSTO-style evaluation -- knights
  wanting outposts more in the endgame, rooks valuing open files more
  as pawns come off, etc. Smaller than it sounds, since `game_phase()`
  already exists and the pattern is established; mostly more tables.
- Delete `include/engine.h` and the empty `build/` directory; add a
  `.gitignore` for the compiled `dchess` binary.
- Extract the duplicated `CP_*` color-pair `#define`s into one shared
  `headers/tui/colors.h`.
- Thread the original `--fen` string through onboarding so `--menu --fen
  "..."` fully composes instead of dropping the FEN.
- Let moves/`"go"`/`"eval"` do something smarter than reject while the
  engine thinks -- e.g. auto-issuing `stop` first when a human tries to
  move mid-think, rather than requiring them to type `stop` themselves.
  Small, since `search_cancel()` already exists (Round 5); mostly a UX
  question of what should happen automatically versus what a person
  should have to ask for explicitly.

**Medium-sized, real payoff, needs local `ncurses` testing:**
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

## 8. Quick reference: verifying any of this yourself

```bash
# Build the real game (needs ncursesw)
make && ./dchess

# Run the engine-only test suites (no ncurses needed)
gcc -Iheaders -Itests -O2 -Wall tests/perft.c src/engine/*.c src/utils/bitboard.c -o /tmp/perft && /tmp/perft
gcc -Iheaders -Itests -O2 -Wall tests/test_movegen.c src/engine/*.c src/utils/bitboard.c -o /tmp/tm && /tmp/tm
gcc -Iheaders -Itests -O2 -Wall tests/test_fen.c src/engine/*.c src/utils/bitboard.c -o /tmp/fen && /tmp/fen
gcc -Iheaders -O2 -Wall tests/test_eval.c src/engine/*.c src/utils/bitboard.c -o /tmp/eval && /tmp/eval
```

All three should print "All ... passed." with no `FAIL` lines. If you add a
new engine feature, adding a matching perft/unit-test case first is the
cheapest insurance available in this codebase -- it's exactly what caught
the en-passant bug described in §3.
