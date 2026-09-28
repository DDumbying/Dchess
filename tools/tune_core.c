#include "tune_core.h"
#include "engine/fen.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tune_add(TuneSet *s, const Position *pos, float result, unsigned game)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 4096;
        s->p = realloc(s->p, (size_t)s->cap * sizeof(TunePos));
    }
    s->p[s->n++] = (TunePos){ *pos, result, game };
}

int tune_load(const char *path, TuneSet *train, TuneSet *valid)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *a = strchr(line, ';'), *b = a ? strchr(a + 1, ';') : NULL;
        if (!b) continue;
        *a = '\0';
        Position pos;
        if (!parse_fen(line, &pos, NULL, NULL)) continue;
        unsigned game = (unsigned)strtoul(b + 1, NULL, 10);
        tune_add(game % 10 == 0 ? valid : train, &pos, (float)atof(a + 1), game);
    }
    fclose(f);
    return 1;
}

double tune_sigmoid(int score, double K)
{
    return 1.0 / (1.0 + pow(10.0, -K * score / 400.0));
}

static EvalOptions terms = { 1, 1, 1, 1, 1 };
static unsigned char mask[EP_COUNT];
static int mask_set;

void tune_set_terms(const EvalOptions *o) { terms = *o; }
void tune_set_mask(const unsigned char *m) { memcpy(mask, m, sizeof(mask)); mask_set = 1; }

static int movable(int i)
{
    if (mask_set) return mask[i];
    return i < EP_MOB_TYPICAL || i >= EP_MOB_TYPICAL + 4;
}

typedef struct { const TuneSet *s; int from, to; double K, sum; } Slice;

static void *slice_error(void *arg)
{
    Slice *sl = arg;
    double sum = 0;
    for (int i = sl->from; i < sl->to; i++) {
        const TunePos *t = &sl->s->p[i];
        int e = evaluate(&t->pos);
        if (t->pos.side == BLACK) e = -e;
        double d = t->result - tune_sigmoid(e, sl->K);
        sum += d * d;
    }
    sl->sum = sum;
    return NULL;
}

double tune_error(const TuneSet *s, double K, int threads)
{
    if (!s->n) return 0;
    eval_set_options(&terms);
    if (threads < 1) threads = 1;
    if (threads > 64) threads = 64;
    pthread_t th[64];
    Slice sl[64];
    for (int i = 0; i < threads; i++) {
        sl[i] = (Slice){ s, (int)((long)s->n * i / threads), (int)((long)s->n * (i + 1) / threads), K, 0 };
        pthread_create(&th[i], NULL, slice_error, &sl[i]);
    }
    double sum = 0;
    for (int i = 0; i < threads; i++) { pthread_join(th[i], NULL); sum += sl[i].sum; }
    return sum / s->n;
}

double tune_fit_k(const TuneSet *s, int threads)
{
    double lo = 0.1, hi = 3.0, g = (sqrt(5.0) - 1) / 2;
    double a = hi - g * (hi - lo), b = lo + g * (hi - lo);
    double fa = tune_error(s, a, threads), fb = tune_error(s, b, threads);
    for (int i = 0; i < 40; i++) {
        if (fa < fb) { hi = b; b = a; fb = fa; a = hi - g * (hi - lo); fa = tune_error(s, a, threads); }
        else         { lo = a; a = b; fa = fb; b = lo + g * (hi - lo); fb = tune_error(s, b, threads); }
    }
    return (lo + hi) / 2;
}

int tune_pass(const TuneSet *s, double K, int threads, double *err)
{
    EvalParams p = *eval_params();
    double best = tune_error(s, K, threads);
    int moved = 0;
    for (int i = 0; i < EP_COUNT; i++) {
        if (!movable(i)) continue;
        for (int dir = 1; dir >= -1; dir -= 2) {
            int stepped = 0;
            for (;;) {                       /* keep going while it helps */
                p.v[i] += dir;
                eval_set_params(&p);
                double e = tune_error(s, K, threads);
                if (e < best) { best = e; stepped = 1; continue; }
                p.v[i] -= dir;
                eval_set_params(&p);
                break;
            }
            if (stepped) { moved++; break; }
        }
    }
    *err = best;
    return moved;
}

void tune_write_header(const char *path, const EvalParams *p)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    fprintf(f, "/* Written by tools/tune.c from Stockfish self-play positions; see\n"
               " * docs/overview.md (round 11). Order: the EP_* enum in eval.h. */\n"
               "#define TUNED_PARAMS {");
    for (int i = 0; i < EP_COUNT; i++) fprintf(f, "%s%d", i ? ", " : " ", p->v[i]);
    fprintf(f, " }\n");
    fclose(f);
}

void tune_write_params(const char *path, const EvalParams *p)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    for (int i = 0; i < EP_COUNT; i++) fprintf(f, "%s%d", i ? " " : "", p->v[i]);
    fputc('\n', f);
    fclose(f);
}

int tune_read_params(const char *path, EvalParams *p)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = 1;
    for (int i = 0; i < EP_COUNT && ok; i++) ok = fscanf(f, "%d", &p->v[i]) == 1;
    fclose(f);
    return ok;
}
