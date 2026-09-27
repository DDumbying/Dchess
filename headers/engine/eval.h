#ifndef EVAL_H
#define EVAL_H

#include "board.h"

int evaluate(const Position *pos);

/* The evaluation's terms, switchable so each one can be measured
 * (tools/match.c). Set before a search starts. */
typedef struct { int pesto, pawns, mobility, king, extras; } EvalOptions;
EvalOptions eval_default_options(void);
void eval_set_options(const EvalOptions *o);

/* The added terms' weights, as signed centipawns per occurrence (a penalty
 * is negative). Flat, so tools/tune.c can adjust them one by one. */
enum {
    EP_DOUBLED_MG, EP_DOUBLED_EG, EP_ISOLATED_MG, EP_ISOLATED_EG,
    EP_PASSED_MG,                    /* 6: relative ranks 1..6 */
    EP_PASSED_EG = EP_PASSED_MG + 6,
    EP_MOB_TYPICAL = EP_PASSED_EG + 6,   /* 4: N B R Q, squares expected */
    EP_MOB_MG = EP_MOB_TYPICAL + 4,
    EP_MOB_EG = EP_MOB_MG + 4,
    EP_SHIELD = EP_MOB_EG + 4, EP_OPEN_FILE,
    EP_PAIR_MG, EP_PAIR_EG, EP_ROOK_OPEN_MG, EP_ROOK_OPEN_EG, EP_ROOK_HALF_MG, EP_ROOK_HALF_EG,
    EP_COUNT
};
typedef struct { int v[EP_COUNT]; } EvalParams;
const EvalParams *eval_default_params(void);   /* the hand-set weights */
const EvalParams *eval_params(void);           /* the ones in use */
void eval_set_params(const EvalParams *p);

#endif
