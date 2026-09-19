#ifndef COMMANDS_H
#define COMMANDS_H

#include "tui/tui.h"
#include <stddef.h>

/* Returns 1 if command was handled, 0 if unknown */
int handle_command(TUIState *state, const char *cmd);

/* Call once per main-loop iteration. If a background engine search
 * (started via the "go" command, or automatically after a human move)
 * has finished, this joins the worker thread, applies its result (plays
 * the move, updates eval/status/history), and returns 1 -- the caller
 * should then re-sync clock_side and call check_game_over(), exactly as
 * it would after handle_command() applies a move. Returns 0 if no search
 * is in flight or it hasn't finished yet -- a cheap, non-blocking check
 * either way. */
int poll_engine_search(TUIState *state);

/* Cancels an in-flight engine search and waits for the worker thread to
 * stop (near-instant: the cancellation flag is checked every ~512 nodes).
 * Whatever the search had found is discarded rather than played.
 *
 * Call this from anywhere that is about to replace the game state --
 * a new game, a loaded position, a side swap. Leaving a search running
 * across such a reset lets poll_engine_search() land a move computed for
 * the *previous* game on the new board. Safe to call when no search is
 * running (it's a no-op). */
void cancel_engine_search(TUIState *state);

/* Start a fresh game: resets the GameState, clears the selection and
 * highlights, flips the board back to White's view and cancels any
 * search belonging to the game being discarded.
 *
 * Both the "new" command and the game-over popup's "R" go through here.
 * They used to each spell the reset out longhand, which is how the popup
 * came to be missing the search cancellation. */
void tui_new_game(TUIState *state);

/* Take back the last move, in whatever sense the current mode makes it
 * mean: one ply in two-player (both sides are human), otherwise as many
 * plies as it takes to hand the turn back to the human -- normally two,
 * or one if the engine has not replied yet.
 *
 * Cancels any search in flight first, since it belongs to a position
 * that is about to stop existing, and deliberately does NOT start a new
 * one afterwards: letting the engine move again would immediately undo
 * the undo. Sets the status line either way. */
void tui_undo(TUIState *state);

/* "Easy" / "Medium" / "Hard" for a DIFF_* constant. */
const char *difficulty_label(int difficulty);

/* Formats the "You play White | Medium difficulty" setup blurb. */
void describe_setup(const TUIState *state, char *buf, size_t n);

#endif
