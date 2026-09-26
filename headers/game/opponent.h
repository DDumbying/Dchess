#ifndef OPPONENT_H
#define OPPONENT_H

#include "engine/board.h"
#include "engine/search.h"
#include "utils/types.h"

/* An engine playing one side. Searches run in the background: start one,
 * then poll from the main loop until its result comes back. */
typedef struct Opponent Opponent;

Opponent *opponent_builtin(int depth, int time_ms);

int  opponent_start (Opponent *o, const Position *pos, U64 key);
int  opponent_poll  (Opponent *o, SearchResult *out, U64 *key);
void opponent_stop  (Opponent *o);
void opponent_cancel(Opponent *o);
void opponent_free  (Opponent *o);

#endif
