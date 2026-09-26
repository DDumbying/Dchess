#include "game/statsview.h"
#include "utils/constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int side_of(const Record *r, const char *who)
{
    if (r->white_kind == KIND_PROFILE && !strcmp(r->white, who)) return WHITE;
    if (r->black_kind == KIND_PROFILE && !strcmp(r->black, who)) return BLACK;
    return -1;
}

static void opponent_name(const Record *r, int side, char *buf, size_t n)
{
    SideKind k = side == WHITE ? r->black_kind : r->white_kind;
    const char *name = side == WHITE ? r->black : r->white;
    const char *str  = side == WHITE ? r->black_strength : r->white_strength;
    if (k == KIND_DCHESS)     snprintf(buf, n, str[0] ? "dchess %s" : "dchess", str);
    else if (k == KIND_GUEST) snprintf(buf, n, "Guest");
    else                      snprintf(buf, n, "%s", name);
}

static void add(RecordTally *t, int o)
{
    t->games++;
    if (o > 0) t->wins++;
    else if (o < 0) t->losses++;
    else t->draws++;
}

static SvOpponent *opponent(StatsView *v, const char *name)
{
    for (int i = 0; i < v->opp_count; i++)
        if (!strcmp(v->opp[i].name, name)) return &v->opp[i];
    if (v->opp_count == SV_OPP_MAX) return NULL;
    SvOpponent *o = &v->opp[v->opp_count++];
    snprintf(o->name, sizeof(o->name), "%s", name);
    return o;
}

static int by_games(const void *a, const void *b)
{
    return ((const SvOpponent *)b)->games - ((const SvOpponent *)a)->games;
}

void stats_view_build(const RecordList *l, const Profile *p, long now, StatsView *v)
{
    memset(v, 0, sizeof(*v));
    const char *who = p->name;
    long plies = 0, seconds = 0;
    int run = 0;

    for (int i = 0; i < l->count; i++) {
        const Record *r = &l->r[i];
        int side = side_of(r, who);
        if (side < 0) continue;
        int o = side == WHITE ? r->result : -r->result;

        char s = o > 0 ? 'W' : o < 0 ? 'L' : 'D';
        v->streak_len = s == v->streak ? v->streak_len + 1 : 1;
        v->streak = s;
        run = o > 0 ? run + 1 : 0;
        if (run > v->best_win_streak) v->best_win_streak = run;

        long t = records_time(r);
        if (t > 0 && t <= now)
            for (int w = 0; w < 4; w++)
                if (t > now - 7L * 86400 * (w + 1) && t <= now - 7L * 86400 * w) v->per_week[w]++;

        if (r->legacy) continue;
        add(&v->total, o);
        add(side == WHITE ? &v->as_white : &v->as_black, o);
        plies += r->plies;
        seconds += r->seconds;

        char name[PLAYER_NAME_MAX + 1];
        opponent_name(r, side, name, sizeof(name));
        SvOpponent *op = opponent(v, name);
        if (op) {
            op->games++;
            if (o > 0) op->wins++;
            else if (o < 0) op->losses++;
            else op->draws++;
            if (t > op->last) op->last = t;
        }

        const char *e = r->end_reason;
        if      (!strcmp(e, "checkmate"))  v->mates++;
        else if (!strcmp(e, "resigned"))   v->resigns++;
        else if (!strcmp(e, "stalemate"))  v->stalemates++;
        else if (!strcmp(e, "repetition")) v->repetitions++;
        else if (!strcmp(e, "fifty-move")) v->fifty++;
        else if (!strcmp(e, "material"))   v->material++;
    }

    if (v->total.games) {
        v->avg_plies   = (int)(plies / v->total.games);
        v->avg_seconds = (int)(seconds / v->total.games);
    }

    if (p->legacy_games > 0) {
        SvOpponent *op = opponent(v, "before profiles");
        if (op) {
            op->games = p->legacy_games;
            op->wins = p->legacy_wins;
            op->losses = p->legacy_losses;
            op->draws = p->legacy_draws;
        }
        v->total.games  += p->legacy_games;
        v->total.wins   += p->legacy_wins;
        v->total.losses += p->legacy_losses;
        v->total.draws  += p->legacy_draws;
    }
    qsort(v->opp, (size_t)v->opp_count, sizeof(v->opp[0]), by_games);

    v->trend_count  = records_winrate(l, who, v->trend, SV_TREND);
    v->recent_count = records_recent(l, who, v->recent, 256);
}
