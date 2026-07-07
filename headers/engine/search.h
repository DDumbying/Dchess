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

/* Asks an in-progress search() (running on another thread) to stop as
 * soon as possible. search() will still return the last iteration that
 * completed cleanly -- never nothing, as long as depth 1 finished (see
 * above). Safe to call whether or not a search is actually running;
 * has no effect until the *next* search() call otherwise (each search()
 * clears this at its own start). Thread-safe: this is the one function
 * in this file meant to be called from a different thread than the one
 * running search() itself. */
void search_cancel(void);

#endif
