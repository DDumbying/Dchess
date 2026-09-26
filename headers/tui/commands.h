#ifndef COMMANDS_H
#define COMMANDS_H

#include "tui/tui.h"
#include <stddef.h>

/* Returns -1 on quit, 1 otherwise. */
int handle_command(TUIState *state, const char *cmd);

/* Call once per main-loop tick. Applies a finished search, or starts the
 * side to move's engine when it is due. Returns 1 when a search came
 * back, in which case the caller should call game_update_status(). */
int drive_turn(TUIState *state);

/* Discards whatever is being searched. No-op when nothing is. */
void cancel_engine_search(TUIState *state);

/* Builds a driver for every engine side. Call once the players are final;
 * tui_release_players() frees them. */
void tui_attach_players(TUIState *state);
void tui_release_players(TUIState *state);

/* Whether the person at the keyboard may move for the side to move. */
int tui_can_move_by_hand(const TUIState *state);

/* Keeps the players and clears any pause. Used by "new" and the game-over
 * popup. */
void tui_new_game(TUIState *state);

/* Takes back players_undo_plies() plies and starts nothing; pauses when
 * both sides are engines, which would otherwise replay the move. */
void tui_undo(TUIState *state);

/* "White: You · Black: dchess Medium" */
void describe_setup(const TUIState *state, char *buf, size_t n);

#endif
