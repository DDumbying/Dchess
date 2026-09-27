/* Labelled positions for tuning the evaluation: Stockfish plays itself from
 * varied openings, and every quiet position is written with the game's
 * result, as "FEN;result;game" (result 1, 0.5 or 0 for White).
 *
 *   ./build/genfens --games 400 --seed 3 --out part.txt [--ms 10] [--engine PATH]
 *
 * make genfens runs several of these in parallel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine/fen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "engine/movegen.h"
#include "game/game.h"
#include "game/openings.h"
#include "game/opponent.h"
#include "game/uci.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include "utils/engines.h"

#define MAX_PLIES  300
#define FIRST_PLY  10

static unsigned rng;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static int play_uci(GameState *g, const char *m)
{
    int from, to, promo;
    Move mv;
    if (!parse_move_str(m, &from, &to, &promo) || !game_find_move(g, from, to, promo, &mv)) return 0;
    game_play(g, mv);
    game_update_status(g);
    return 1;
}

static int random_move(GameState *g)
{
    MoveList ml;
    Move legal[MAX_MOVES];
    int n = 0;
    generate_moves(&g->pos, &ml);
    for (int i = 0; i < ml.count; i++) {
        Position t = g->pos;
        if (make_move(&t, ml.moves[i])) legal[n++] = ml.moves[i];
    }
    if (!n) return 0;
    game_play(g, legal[rnd() % n]);
    game_update_status(g);
    return 1;
}

int main(int argc, char **argv)
{
    int games = 100, ms = 10;
    unsigned seed = 1;
    const char *out = "fens.txt", *engine = "/usr/bin/stockfish";
    for (int i = 1; i + 1 < argc; i += 2) {
        if      (!strcmp(argv[i], "--games"))  games = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--seed"))   seed = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ms"))     ms = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--out"))    out = argv[i + 1];
        else if (!strcmp(argv[i], "--engine")) engine = argv[i + 1];
        else { fprintf(stderr, "unknown flag %s\n", argv[i]); return 2; }
    }
    init_attacks();
    rng = seed * 2654435761u + 1;
    FILE *f = fopen(out, "w");
    if (!f) { perror(out); return 1; }
    EngineEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "stockfish");
    snprintf(e.path, sizeof(e.path), "%s", engine);
    e.limit_ms = ms;
    Opponent *sf = opponent_uci(&e);

    static GameState g;
    static char fens[MAX_PLIES][FEN_BUFSIZE];
    long positions = 0;
    int failed = 0, capped = 0;
    struct timespec nap = { 0, 1000000L };
    for (int gi = 0; gi < games; gi++) {
        memset(&g, 0, sizeof(g));
        game_reset(&g);
        char line[512], *save = NULL;
        snprintf(line, sizeof(line), "%s", OPENINGS[rnd() % OPENINGS_COUNT].moves);
        int cut = 4 + (int)(rnd() % 5), n = 0;
        for (char *t = strtok_r(line, " ", &save); t && n < cut; t = strtok_r(NULL, " ", &save), n++)
            if (!play_uci(&g, t)) break;
        if (!random_move(&g) || !random_move(&g) || g.game_over) continue;

        int count = 0, bad = 0;
        while (!g.game_over && g.move_count < MAX_PLIES) {
            Move last = g.move_made[g.move_count - 1];
            int quiet = !(FLAGS(last) & (FLAG_CAPTURE | FLAG_PROMOTION)) &&
                        !is_in_check(&g.pos, g.pos.side);
            if (g.move_count >= FIRST_PLY && quiet && count < MAX_PLIES)
                position_to_fen(&g.pos, g.halfmove_clock, g.move_count / 2 + 1, fens[count++], FEN_BUFSIZE);
            SearchResult r;
            U64 key;
            if (!opponent_start(sf, &g)) { bad = 1; break; }
            while (!opponent_poll(sf, &r, &key)) nanosleep(&nap, NULL);
            const char *err = opponent_error(sf);
            if ((err && err[0]) || !r.best_move) { bad = 1; break; }
            game_play(&g, r.best_move);
            game_update_status(&g);
        }
        if (bad) { failed++; continue; }
        if (!g.game_over) capped++;
        const char *res = strstr(g.result, "White wins") ? "1" : strstr(g.result, "Black wins") ? "0" : "0.5";
        for (int i = 0; i < count; i++) fprintf(f, "%s;%s;%u\n", fens[i], res, seed * 100000u + (unsigned)gi);
        positions += count;
    }
    opponent_free(sf);
    fclose(f);
    fprintf(stderr, "seed %u: %d games, %ld positions, %d failed, %d capped at %d plies\n",
            seed, games, positions, failed, capped, MAX_PLIES);
    return 0;
}
