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
 * runs to max_depth). Depth 1 ignores the time limit, so a legal move
 * always comes back (as long as one exists) even under a very tight
 * budget.
 *
 * The one case where best_move can still come back 0 with legal moves
 * on the board is search_cancel() landing before depth 1 finished --
 * cancellation is deliberately checked at every depth so that quitting
 * and "start over" commands stay responsive. Callers MUST therefore
 * treat an empty best_move as "the search produced nothing", and use
 * has_legal_moves() to decide whether the game is actually over. */
SearchResult search(Position *pos, int max_depth, int time_limit_ms);

/* Asks an in-progress search() (running on another thread) to stop as
 * soon as possible. search() returns the last iteration that completed
 * cleanly, which is an empty result if it was cancelled during depth 1
 * (see above). Safe to call whether or not a search is actually running;
 * has no effect until the *next* search() call otherwise (each search()
 * clears this at its own start). Thread-safe: this is the one function
 * in this file meant to be called from a different thread than the one
 * running search() itself. */
void search_cancel(void);

#endif
