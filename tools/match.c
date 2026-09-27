/* Plays dchess's search against itself with different options, to measure
 * what a technique is worth.
 *
 *   make match ARGS="--base none --cand pvs --games 80 --ms 50"
 *
 * Options are a comma list starting from all or none: pvs, asp, nmp, lmr,
 * ext, tt; "-name" turns one off. Every opening is played twice, colours
 * swapped. The result is the candidate's score and the Elo difference with
 * a 95% error. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/move.h"
#include "engine/search.h"
#include "game/game.h"
#include "game/openings.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

#define OPENING_PLIES 8
#define MAX_PLIES     200

static int parse_options(const char *list, SearchOptions *o)
{
    char buf[128], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", list);
    memset(o, 0, sizeof(*o));
    for (char *t = strtok_r(buf, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        int on = t[0] != '-';
        const char *n = on ? t : t + 1;
        if      (!strcmp(n, "all"))  { *o = search_default_options(); if (!on) memset(o, 0, sizeof(*o)); }
        else if (!strcmp(n, "none")) memset(o, 0, sizeof(*o));
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

/* +1 when the candidate wins, -1 when it loses, 0 for a draw. */
static int play_game(int opening, int cand_side, const SearchOptions *base,
                     const SearchOptions *cand, int ms)
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
        search_set_options(g.pos.side == cand_side ? cand : base);
        search_clear();
        Position p = g.pos;
        SearchResult r = search(&p, MAX_DEPTH, ms);
        if (!r.best_move) break;
        game_play(&g, r.best_move);
        game_update_status(&g);
    }
    int winner = strstr(g.result, "White wins") ? WHITE : strstr(g.result, "Black wins") ? BLACK : -1;
    return winner < 0 ? 0 : winner == cand_side ? 1 : -1;
}

int main(int argc, char **argv)
{
    int games = 80, ms = 50;
    const char *base_s = "none", *cand_s = "all";
    for (int i = 1; i + 1 < argc; i += 2) {
        if      (!strcmp(argv[i], "--games")) games = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ms"))    ms = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--base"))  base_s = argv[i + 1];
        else if (!strcmp(argv[i], "--cand"))  cand_s = argv[i + 1];
        else { fprintf(stderr, "unknown flag %s\n", argv[i]); return 2; }
    }
    SearchOptions base, cand;
    if (!parse_options(base_s, &base) || !parse_options(cand_s, &cand)) return 2;
    if (games < 2) games = 2;
    games += games % 2;
    init_attacks();

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
