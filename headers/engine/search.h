#ifndef SEARCH_H
#define SEARCH_H

#include "board.h"
#include "move.h"

typedef struct {
    Move best_move;
    int  best_score;
    long nodes;
    int  depth_reached; /* deepest iteration fully completed */
    long elapsed_ms;
    Move pv[8];         /* the expected line, starting with best_move */
    int  pv_len;
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

/* The search's techniques, each switchable so their worth can be measured
 * (tools/match.c). Set before a search starts; the defaults are the ones
 * that measured as gains (all but tt_depth). */
typedef struct { int pvs, aspiration, null_move, lmr, check_ext, tt_depth; } SearchOptions;
SearchOptions search_default_options(void);
void search_set_options(const SearchOptions *o);

/* After each completed depth, from the searching thread (UCI "info"). */
typedef struct { int depth, score; long nodes, ms; Move pv[8]; int pv_len; } SearchInfo;
void search_set_info(void (*fn)(const SearchInfo *));

/* Thread-safe; the one function here meant to be called from a different
 * thread than search() itself. Safe to call when nothing is running. */
void search_cancel(void);

/* Forget everything the transposition table has learned. */
void search_clear(void);

#endif
