#ifndef EVAL_H
#define EVAL_H

#include "board.h"

int evaluate(const Position *pos);

/* The evaluation's terms, switchable so each one can be measured
 * (tools/match.c). Set before a search starts. */
typedef struct { int pesto, pawns, mobility, king, extras; } EvalOptions;
EvalOptions eval_default_options(void);
void eval_set_options(const EvalOptions *o);

#endif
