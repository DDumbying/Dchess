#ifndef TUNE_CORE_H
#define TUNE_CORE_H

#include "engine/board.h"
#include "engine/eval.h"

/* One labelled position: the game's result for White (1, 0.5, 0). */
typedef struct { Position pos; float result; unsigned game; } TunePos;
typedef struct { TunePos *p; int n, cap; } TuneSet;

void   tune_add(TuneSet *s, const Position *pos, float result, unsigned game);
/* Every 10th game goes to `valid`, so no game is split across the two. */
int    tune_load(const char *path, TuneSet *train, TuneSet *valid);
double tune_sigmoid(int score, double K);
/* Mean squared error of sigmoid(evaluation for White) against the results,
 * with every evaluation term on. */
double tune_error(const TuneSet *s, double K, int threads);
/* The evaluation terms the error uses (default: every one), and which
 * parameters a pass may move (default: all but mobility's expected counts). */
void   tune_set_terms(const EvalOptions *o);
void   tune_set_mask(const unsigned char *mask);
double tune_fit_k(const TuneSet *s, int threads);
/* One sweep over every parameter: each moves while it lowers the error.
 * Returns how many moved; *err is the error afterwards. */
int    tune_pass(const TuneSet *s, double K, int threads, double *err);
void   tune_write_header(const char *path, const EvalParams *p);
/* One line of EP_COUNT integers, for tools/match.c's --*-params. */
void   tune_write_params(const char *path, const EvalParams *p);
int    tune_read_params(const char *path, EvalParams *p);

#endif
