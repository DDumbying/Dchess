#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "game/game.h"
#include "utils/engines.h"

typedef struct {
    int  score_cp;            /* White's view */
    int  mate;                /* 0, or mate in N (+ White mates, - Black mates) */
    int  depth;
    Move best;                /* 0 if none */
    char line[8][8]; int line_len;   /* SAN, starting with best */
} Analysis;

typedef struct Analyser Analyser;
Analyser *analyser_builtin(int time_ms);
Analyser *analyser_uci(const EngineEntry *e, int time_ms);
void analyser_free(Analyser *a);
/* Starts on `g`, cancelling any search running; 0 when there is nothing
 * to analyse (the game is over). */
int  analyser_start(Analyser *a, const GameState *g);
/* 1 when a result is ready, with the game_hash() it was started for. */
int  analyser_poll(Analyser *a, Analysis *out, U64 *key);
void analyser_stop(Analyser *a);
int  analyser_is_builtin(const Analyser *a);
/* The driver's last error, or "". */
const char *analyser_error(const Analyser *a);

/* How much a move lost for the side that played it, graded. */
typedef enum { GRADE_NONE, GRADE_INACCURACY, GRADE_MISTAKE, GRADE_BLUNDER } Grade;
Grade review_grade(const Analysis *before, const Analysis *after, int mover);
/* The centipawns the mover lost (clamped scores); negative when it gained. */
int   review_loss(const Analysis *before, const Analysis *after, int mover);
const char *grade_mark(Grade g);   /* "", "?!", "?", "??" */

#endif
