/* Plays dchess's search against itself with different options, to measure
 * what a technique is worth.
 *
 *   make match ARGS="--base none --cand pvs --games 80 --ms 50"
 *
 * Options are a comma list starting from all (the defaults) or none:
 * search pvs, asp, nmp, lmr, ext, tt; evaluation pesto, pawns, mob, king,
 * xtra; "-name" turns one off. Every opening is played twice, colours
 * swapped. The result is the candidate's score and the Elo difference with
 * a 95% error.
 *
 * --vs PATH plays the candidate against a UCI engine instead (with
 * --vs-elo N for UCI_Elo and --vs-ms N for its time a move), which puts
 * dchess on that engine's rating scale. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/move.h"
#include "engine/search.h"
#include "engine/eval.h"
#include "game/game.h"
#include "game/openings.h"
#include "game/opponent.h"
#include "game/uci.h"
#include "utils/engines.h"
#include <time.h>
#include "utils/bitboard.h"
#include "utils/constants.h"

#define OPENING_PLIES 8
#define MAX_PLIES     200

/* A player's search and evaluation switches. */
typedef struct { SearchOptions s; EvalOptions e; } Opts;

static int parse_options(const char *list, Opts *all)
{
    char buf[128], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", list);
    memset(all, 0, sizeof(*all));
    SearchOptions *o = &all->s;
    EvalOptions *e = &all->e;
    for (char *t = strtok_r(buf, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        int on = t[0] != '-';
        const char *n = on ? t : t + 1;
        if (!strcmp(n, "all")) {
            *o = search_default_options();
            *e = eval_default_options();
            if (!on) memset(all, 0, sizeof(*all));
        }
        else if (!strcmp(n, "none")) memset(all, 0, sizeof(*all));
        else if (!strcmp(n, "pesto")) e->pesto = on;
        else if (!strcmp(n, "pawns")) e->pawns = on;
        else if (!strcmp(n, "mob"))   e->mobility = on;
        else if (!strcmp(n, "king"))  e->king = on;
        else if (!strcmp(n, "xtra"))  e->extras = on;
        else if (!strcmp(n, "pvs"))  o->pvs = on;
        else if (!strcmp(n, "asp"))  o->aspiration = on;
        else if (!strcmp(n, "nmp"))  o->null_move = on;
        else if (!strcmp(n, "lmr"))  o->lmr = on;
        else if (!strcmp(n, "ext"))  o->check_ext = on;
        else if (!strcmp(n, "tt"))   o->tt_depth = on;
        else { fprintf(stderr, "unknown option %s\n", n); return 0; }
    }
    return 1;
}

static int play_uci(GameState *g, const char *m)
{
    int from, to, promo;
    Move mv;
    if (!parse_move_str(m, &from, &to, &promo) || !game_find_move(g, from, to, promo, &mv)) return 0;
    game_play(g, mv);
    game_update_status(g);
    return 1;
}

static Opponent *vs;   /* --vs: the external engine, or NULL */

/* The external engine's move for `g`, or 0 when it fails. */
static Move vs_move(const GameState *g)
{
    SearchResult r;
    U64 key;
    struct timespec nap = { 0, 2 * 1000000L };
    if (!opponent_start(vs, g)) return 0;
    while (!opponent_poll(vs, &r, &key)) nanosleep(&nap, NULL);
    const char *err = opponent_error(vs);
    if (err && err[0]) { fprintf(stderr, "\nopponent: %s\n", err); return 0; }
    return r.best_move;
}

/* +1 when the candidate wins, -1 when it loses, 0 for a draw. */
static int play_game(int opening, int cand_side, const Opts *base, const Opts *cand, int ms)
{
    static GameState g;
    memset(&g, 0, sizeof(g));
    game_reset(&g);
    char line[512], *save = NULL;
    snprintf(line, sizeof(line), "%s", OPENINGS[opening].moves);
    int n = 0;
    for (char *t = strtok_r(line, " ", &save); t && n < OPENING_PLIES; t = strtok_r(NULL, " ", &save), n++)
        if (!play_uci(&g, t)) break;

    while (!g.game_over && g.move_count < MAX_PLIES) {
        Move m;
        if (vs && g.pos.side != cand_side) {
            m = vs_move(&g);
        } else {
            const Opts *me = g.pos.side == cand_side ? cand : base;
            search_set_options(&me->s);
            eval_set_options(&me->e);
            search_clear();
            Position p = g.pos;
            m = search(&p, MAX_DEPTH, ms).best_move;
        }
        if (!m) break;
        game_play(&g, m);
        game_update_status(&g);
    }
    int winner = strstr(g.result, "White wins") ? WHITE : strstr(g.result, "Black wins") ? BLACK : -1;
    return winner < 0 ? 0 : winner == cand_side ? 1 : -1;
}

int main(int argc, char **argv)
{
    int games = 80, ms = 50, vs_elo = 0, vs_ms = 0;
    const char *base_s = "none", *cand_s = "all", *vs_path = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if      (!strcmp(argv[i], "--games")) games = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ms"))    ms = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--base"))  base_s = argv[i + 1];
        else if (!strcmp(argv[i], "--cand"))  cand_s = argv[i + 1];
        else if (!strcmp(argv[i], "--vs"))    vs_path = argv[i + 1];
        else if (!strcmp(argv[i], "--vs-elo")) vs_elo = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--vs-ms")) vs_ms = atoi(argv[i + 1]);
        else { fprintf(stderr, "unknown flag %s\n", argv[i]); return 2; }
    }
    Opts base, cand;
    if (!parse_options(base_s, &base) || !parse_options(cand_s, &cand)) return 2;
    if (games < 2) games = 2;
    games += games % 2;
    init_attacks();
    char vs_name[128] = "";
    if (vs_path) {
        EngineEntry e;
        memset(&e, 0, sizeof(e));
        snprintf(e.name, sizeof(e.name), "opponent");
        snprintf(e.path, sizeof(e.path), "%s", vs_path);
        e.limit_ms = vs_ms > 0 ? vs_ms : ms;
        e.elo = vs_elo;
        vs = opponent_uci(&e);
        if (!vs) { fprintf(stderr, "cannot start %s\n", vs_path); return 2; }
        const char *b = strrchr(vs_path, '/');
        if (vs_elo) snprintf(vs_name, sizeof(vs_name), "%s @%d", b ? b + 1 : vs_path, vs_elo);
        else        snprintf(vs_name, sizeof(vs_name), "%s", b ? b + 1 : vs_path);
        base_s = vs_name;
    }

    int w = 0, d = 0, l = 0, pairs = games / 2;
    for (int i = 0; i < pairs; i++) {
        int opening = (int)((long)i * OPENINGS_COUNT / pairs) % OPENINGS_COUNT;
        for (int side = WHITE; side <= BLACK; side++) {
            int r = play_game(opening, side, &base, &cand, ms);
            if (r > 0) w++; else if (r < 0) l++; else d++;
            fprintf(stderr, "\r%d/%d  +%d =%d -%d", w + d + l, games, w, d, l);
        }
    }
    fprintf(stderr, "\n");
    if (vs) opponent_free(vs);

    int n = w + d + l;
    double s = (w + 0.5 * d) / n;
    double var = (w * pow(1 - s, 2) + d * pow(0.5 - s, 2) + l * pow(0 - s, 2)) / n;
    double se = sqrt(var / n);
    double lo = s - 1.96 * se, hi = s + 1.96 * se;
    #define ELO(x) (-400.0 * log10(1.0 / ((x) < 0.001 ? 0.001 : (x) > 0.999 ? 0.999 : (x)) - 1.0))
    printf("%s vs %s, %d games at %d ms: +%d =%d -%d  score %.1f%%  Elo %+.0f (%+.0f .. %+.0f)\n",
           cand_s, base_s, n, ms, w, d, l, 100 * s, ELO(s), ELO(lo), ELO(hi));
    return 0;
}
