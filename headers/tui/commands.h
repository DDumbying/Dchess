#ifndef COMMANDS_H
#define COMMANDS_H

#include "tui/tui.h"
#include <stddef.h>

/* Returns 1 if command was handled, 0 if unknown */
int handle_command(TUIState *state, const char *cmd);

/* Call once per main-loop iteration. Returns 1 if a finished search was
 * applied, in which case the caller should call game_update_status().
 * Non-blocking. */
int poll_engine_search(TUIState *state);

/* Discards whatever the search had found. Call before replacing the game
 * state, or a move computed for the previous position lands on the new
 * one. No-op when nothing is running. */
void cancel_engine_search(TUIState *state);

/* Resets the game and the view, cancelling any search belonging to the
 * game being discarded. Used by both "new" and the game-over popup. */
void tui_new_game(TUIState *state);

/* One ply in two-player, otherwise as many as it takes to hand the turn
 * back to the human. Cancels any running search and deliberately does
 * not start a new one -- the engine moving again would undo the undo. */
void tui_undo(TUIState *state);

/* "Easy" / "Medium" / "Hard" for a DIFF_* constant. */
const char *difficulty_label(int difficulty);

/* "You play White | Medium difficulty" */
void describe_setup(const TUIState *state, char *buf, size_t n);

#endif
