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
 * time_limit_ms elapses (0 or negative means no time limit -- always
 * runs to max_depth). Depth 1 always completes regardless of the time
 * limit, so the caller always gets back a legal move (as long as one
 * exists) even under a very tight budget. */
SearchResult search(Position *pos, int max_depth, int time_limit_ms);

#endif
