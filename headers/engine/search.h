#ifndef SEARCH_H
#define SEARCH_H

#include "board.h"
#include "move.h"

typedef struct {
    Move best_move;
    int  best_score;
    long nodes;
    int  depth_reached; /* deepest iteration fully completed */
} SearchResult;

/* Iterative deepening: searches depth 1, 2, 3, ... up to max_depth,
 * keeping the best fully-completed iteration's result. Stops early if
 * time_limit_ms elapses (0 or negative means no limit). Depth 1 ignores
 * the limit, so a legal move always comes back.
 *
 * EXCEPT when search_cancel() lands before depth 1 finished, which
 * returns an empty best_move. Callers must not read that as "game over"
 * -- use has_legal_moves() for that. */
SearchResult search(Position *pos, int max_depth, int time_limit_ms);

/* Thread-safe; the one function here meant to be called from a different
 * thread than search() itself. Safe to call when nothing is running. */
void search_cancel(void);

/* Forget everything the transposition table has learned. */
void search_clear(void);

#endif
