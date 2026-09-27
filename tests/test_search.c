/* Search strength and correctness: mates, zugzwang, tactics, and that the
 * options switched off give the plain search.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "engine/board.h"
#include "engine/fen.h"
#include "engine/move.h"
#include "engine/search.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

/* Win At Chess positions the search should solve in TACTIC_MS each. The
 * count must not fall below TACTICS_FLOOR, the plain search's result. */
#define TACTIC_MS     500
#define TACTICS_FLOOR 18

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static SearchResult run(const char *fen, int depth, int ms, char *best)
{
    Position pos;
    parse_fen(fen, &pos, NULL, NULL);
    search_clear();
    SearchResult r = search(&pos, depth, ms);
    move_to_str(r.best_move, best);
    return r;
}

static void test_mates(void)
{
    printf("== mates ==\n");
    struct { const char *fen, *move; int moves; } m[] = {
        { "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", "a1a8", 1 },
        { "kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", "a1a6", 2 },          /* Morphy */
        { "r5rk/5p1p/5R2/4B3/8/8/7P/7K w - - 0 1", NULL, 3 },
    };
    for (size_t i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        char best[8], name[64];
        SearchResult r = run(m[i].fen, 8, 0, best);
        snprintf(name, sizeof(name), "mate in %d found at the right distance", m[i].moves);
        check(name, r.best_score == MATE_SCORE - (2 * m[i].moves - 1) &&
                    (!m[i].move || !strcmp(best, m[i].move)));
    }
}

static void test_zugzwang(void)
{
    printf("== zugzwang ==\n");
    char best[8];
    run("8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1", 9, 0, best);
    check("the only non-losing move is found", !strcmp(best, "e1f1"));
}

static void test_plain_search(void)
{
    printf("== options off ==\n");
    static const struct { const char *fen, *move; } b[] = {
        { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "b1c3" },
        { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", "d5e6" },
        { "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 0 8", "d1b3" },
        { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", "c4c5" },
        { "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", "d7c8q" },
        { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", "b4f4" },
    };
    SearchOptions none;
    memset(&none, 0, sizeof(none));
    search_set_options(&none);
    int same = 1;
    for (size_t i = 0; i < sizeof(b) / sizeof(b[0]); i++) {
        char best[8];
        run(b[i].fen, 5, 0, best);
        if (strcmp(best, b[i].move)) { same = 0; printf("    %s gave %s\n", b[i].move, best); }
    }
    check("with every option off the search is unchanged", same);
    SearchOptions all = search_default_options();
    search_set_options(&all);
}

static void test_tactics(void)
{
    printf("== tactics ==\n");
    static const struct { const char *fen, *move; } wac[] = {
        { "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1", "g3g6" },
        { "8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - - 0 1", "b3b2" },
        { "5rk1/1ppb3p/p1pb4/6q1/3P1p1r/2P1R2P/PP1BQ1P1/5RKN w - - 0 1", "e3g3" },
        { "r1bq2rk/pp3pbp/2p1p1pQ/7P/3P4/2PB1N2/PP3PPR/2KR4 w - - 0 1", "h6h7" },
        { "5k2/6pp/p1qN4/1p1p4/3P4/2PKP2Q/PP3r2/3R4 b - - 0 1", "c6c4" },
        { "7k/p7/1R5K/6r1/6p1/6P1/8/8 w - - 0 1", "b6b7" },
        { "rnbqkb1r/pppp1ppp/8/4P3/6n1/7P/PPPNPPP1/R1BQKBNR b KQkq - 0 1", "g4e3" },
        { "r4q1k/p2bR1rp/2p2Q1N/5p2/5p2/2P5/PP3PPP/R5K1 w - - 0 1", "e7f7" },
        { "3q1rk1/p4pp1/2pb3p/3p4/6Pr/1PNQ4/P1PB1PP1/4RRK1 b - - 0 1", "d6h2" },
        { "2br2k1/2q3rn/p2NppQ1/2p1P3/Pp5R/4P3/1P3PPP/3R2K1 w - - 0 1", "h4h7" },
        { "r1b1kb1r/3q1ppp/pBp1pn2/8/Np3P2/5B2/PPP3PP/R2Q1RK1 w kq - 0 1", "f3c6" },
        { "4k1r1/2p3r1/1pR1p3/3pP2p/3P2qP/P4N2/1PQ4P/5R1K b - - 0 1", "g4f3" },
        { "5rk1/pp4p1/2n1p2p/2Npq3/2p5/6P1/P3P1BP/R4Q1K w - - 0 1", "f1f8" },
        { "r2rb1k1/pp1q1p1p/2n1p1p1/2bp4/5P2/PP1BPR1Q/1BPN2PP/R5K1 w - - 0 1", "h3h7" },
        { "1R6/1brk2p1/4p2p/p1P1Pp2/P7/6P1/1P4P1/2R3K1 w - - 0 1", "b8b7" },
        { "r4rk1/ppp2ppp/2n5/2bqp3/8/P2PB3/1PP1NPPP/R2Q1RK1 w - - 0 1", "e2c3" },
        { "R7/P4k2/8/8/8/8/r7/6K1 w - - 0 1", "a8h8" },
        { "r1b2rk1/ppbn1ppp/4p3/1QP4q/3P4/N4N2/5PPP/R1B2RK1 w - - 0 1", "c5c6" },
        { "r2qkb1r/1ppb1ppp/p7/4p3/P1Q1P3/2P5/5PPP/R1B2KNR b kq - 0 1", "d7b5" },
        { "5rk1/1b3p1p/pp3p2/3n1N2/1P6/P1qB1PP1/3Q3P/4R1K1 w - - 0 1", "d2h6" },
    };
    int n = (int)(sizeof(wac) / sizeof(wac[0])), solved = 0;
    for (int i = 0; i < n; i++) {
        char best[8];
        run(wac[i].fen, MAX_DEPTH, TACTIC_MS, best);
        solved += !strcmp(best, wac[i].move);
    }
    char name[64];
    snprintf(name, sizeof(name), "%d of %d tactics solved (floor %d)", solved, n, TACTICS_FLOOR);
    check(name, solved >= TACTICS_FLOOR);
}

int main(void)
{
    init_attacks();
    test_mates();
    test_zugzwang();
    test_plain_search();
    test_tactics();
    if (failures) {
        printf("\n%d search test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll search tests passed.\n");
    return 0;
}
