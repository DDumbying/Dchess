#ifndef COMMANDS_H
#define COMMANDS_H

#include "tui/tui.h"

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

#endif
