#include "game/analysis.h"
#include "game/opponent.h"
#include "utils/cli.h"
#include "game/san.h"
#include "game/uci.h"
#include "engine/make.h"
#include "engine/movegen.h"
#include "utils/constants.h"
#include <stdlib.h>
#include <string.h>

struct Analyser {
    Opponent *o;
    int       builtin;
    Position  pos;           /* the position analysed, for SAN */
};

static Analyser *wrap(Opponent *o, int builtin)
{
    if (!o) return NULL;
    Analyser *a = calloc(1, sizeof(*a));
    if (!a) { opponent_free(o); return NULL; }
    a->o = o;
    a->builtin = builtin;
    return a;
}

Analyser *analyser_builtin(int time_ms)
{
    return wrap(opponent_builtin(MAX_DEPTH, time_ms, NULL, DIFF_HARD), 1);
}

Analyser *analyser_uci(const EngineEntry *e, int time_ms)
{
    EngineEntry c = *e;
    c.limit_ms = time_ms;
    c.limit_depth = 0;
    c.elo = 0;                  /* analysis wants full strength */
    return wrap(opponent_uci(&c), 0);
}

void analyser_free(Analyser *a)
{
    if (!a) return;
    opponent_free(a->o);
    free(a);
}

int analyser_start(Analyser *a, const GameState *g)
{
    if (!a || g->game_over || !has_legal_moves(&g->pos)) return 0;
    opponent_cancel(a->o);
    a->pos = g->pos;
    return opponent_start(a->o, g);
}

void analyser_stop(Analyser *a)            { if (a) opponent_cancel(a->o); }
int  analyser_is_builtin(const Analyser *a) { return a && a->builtin; }
const char *analyser_error(const Analyser *a) { return a ? opponent_error(a->o) : ""; }

int analyser_poll(Analyser *a, Analysis *out, U64 *key)
{
    SearchResult r;
    if (!a || !opponent_poll(a->o, &r, key)) return 0;
    memset(out, 0, sizeof(*out));
    int sign = a->pos.side == WHITE ? 1 : -1, s = r.best_score;
    if (abs(s) > MATE_BOUND) {
        int n = MATE_SCORE - abs(s);        /* plies for dchess, moves for UCI */
        if (a->builtin) n = (n + 1) / 2;
        if (n < 1) n = 1;
        out->mate = (s > 0 ? n : -n) * sign;
        out->score_cp = out->mate > 0 ? 30000 - n : -30000 + n;
    } else {
        out->score_cp = s * sign;
    }
    out->depth = r.depth_reached;
    out->best  = r.best_move;
    Move line[8];
    int n = r.pv_len;
    memcpy(line, r.pv, sizeof(line));
    if (!n && r.best_move) { line[0] = r.best_move; n = 1; }
    Position p = a->pos;
    for (int i = 0; i < n; i++) {
        Position next = p;
        if (!make_move(&next, line[i])) break;
        san_write(&p, line[i], out->line[out->line_len++]);
        p = next;
    }
    return 1;
}

#define GRADE_CLAMP 1500

/* A score from the mover's side, clamped so won positions do not grade
 * every quiet move. */
static int mover_view(const Analysis *a, int mover)
{
    int v = a->mate ? (a->mate > 0 ? GRADE_CLAMP : -GRADE_CLAMP) : a->score_cp;
    if (v > GRADE_CLAMP) v = GRADE_CLAMP;
    if (v < -GRADE_CLAMP) v = -GRADE_CLAMP;
    return mover == WHITE ? v : -v;
}

int review_loss(const Analysis *before, const Analysis *after, int mover)
{
    return mover_view(before, mover) - mover_view(after, mover);
}

Grade review_grade(const Analysis *before, const Analysis *after, int mover)
{
    int sign = mover == WHITE ? 1 : -1;
    if (before->mate * sign > 0 && after->mate * sign > 0) return GRADE_NONE;
    int loss = review_loss(before, after, mover);
    return loss >= 300 ? GRADE_BLUNDER : loss >= 100 ? GRADE_MISTAKE
         : loss >= 50  ? GRADE_INACCURACY : GRADE_NONE;
}

const char *grade_mark(Grade g)
{
    static const char *marks[] = { "", "?!", "?", "??" };
    return marks[g];
}
