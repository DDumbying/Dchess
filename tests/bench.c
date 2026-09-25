/* Search benchmark: nodes and time to reach a fixed depth.
 *
 * Move ordering changes how much of the tree gets searched, not the
 * answer, so the number that matters is total nodes -- lower is better,
 * and the scores should stay put.
 *
 * Build & run:  make bench
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "engine/board.h"
#include "engine/fen.h"
#include "engine/move.h"
#include "engine/search.h"
#include "utils/bitboard.h"

static const struct { const char *name, *fen; } POSITIONS[] = {
    { "start",      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" },
    { "kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" },
    { "middlegame", "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 0 8" },
    { "tactical",   "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1" },
    { "promotion",  "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8" },
    { "endgame",    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1" },
};

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    int depth = argc > 1 ? atoi(argv[1]) : 6;
    init_attacks();

    long total_nodes = 0;
    double total_ms = 0;

    printf("depth %d\n", depth);
    printf("%-11s %12s %9s %7s  %s\n", "position", "nodes", "ms", "score", "best");

    for (size_t i = 0; i < sizeof(POSITIONS) / sizeof(POSITIONS[0]); i++) {
        Position pos;
        int hm, fm;
        if (!parse_fen(POSITIONS[i].fen, &pos, &hm, &fm)) {
            printf("%-11s  bad FEN\n", POSITIONS[i].name);
            return 1;
        }

        search_clear();   /* each position starts cold */
        double t0 = now_ms();
        SearchResult r = search(&pos, depth, 0);
        double ms = now_ms() - t0;

        char mv[8];
        move_to_str(r.best_move, mv);
        printf("%-11s %12ld %9.0f %7d  %s\n", POSITIONS[i].name, r.nodes, ms, r.best_score, mv);

        total_nodes += r.nodes;
        total_ms += ms;
    }

    printf("%-11s %12ld %9.0f\n", "TOTAL", total_nodes, total_ms);
    return 0;
}
