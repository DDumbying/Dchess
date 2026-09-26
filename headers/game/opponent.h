#ifndef OPPONENT_H
#define OPPONENT_H

#include "engine/search.h"
#include "game/game.h"
#include "utils/types.h"

/* An engine playing one side. Searches run in the background: start one,
 * then poll from the main loop until its result comes back. */
typedef struct Opponent Opponent;

Opponent *opponent_builtin(int depth, int time_ms);

/* 0 while a search is already running. */
int  opponent_start (Opponent *o, const GameState *g);

/* 1 once a result is ready, with game_hash() of the game it was started
 * for. A failed search polls as a result with no move. */
int  opponent_poll  (Opponent *o, SearchResult *out, U64 *key);

/* Finish now; the next poll returns the best move found so far. */
void opponent_stop  (Opponent *o);

/* Stop and throw the result away. */
void opponent_cancel(Opponent *o);
void opponent_free  (Opponent *o);

/* NULL, or why the engine stopped working. */
const char *opponent_error(const Opponent *o);

#endif
