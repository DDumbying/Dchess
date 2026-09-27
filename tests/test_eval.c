/* Evaluation unit tests -- mainly a regression guard for a real bug
 * found and fixed while adding king PST tapering: the piece-square
 * tables (king_pst, pawn_pst, rook_pst, ...) are written in the
 * standard chess-programming convention (row 0 = rank 8, row 7 =
 * rank 1), not in this engine's native a1=0 square order, and the
 * lookup needs mirror() on White specifically to account for that --
 * not on Black, as it originally was. Getting this backwards makes the
 * engine score king safety and pawn advancement exactly backwards for
 * both sides (see docs/overview.md for the full story).
 *
 * Build & run:
 *   gcc -Iheaders -O2 tests/test_eval.c src/engine/[a-z]*.c src/utils/bitboard.c -o /tmp/test_eval
 *   /tmp/test_eval
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "engine/board.h"
#include "engine/eval.h"
#include "engine/fen.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* A full standard non-pawn complement (2N+2B+2R+1Q per side), placed on
 * mirrored squares so the two sides' contributions are always exactly
 * equal and cancel in any white-vs-black comparison -- this pins
 * game_phase() to its maximum (pure middlegame) without disturbing
 * which position "wins" on any other basis, isolating whatever single
 * piece the test is actually about. */
static void add_full_material(Position *pos)
{
    SET_BIT(pos->bitboards[N], b1); SET_BIT(pos->bitboards[N], g1);
    SET_BIT(pos->bitboards[B], c1); SET_BIT(pos->bitboards[B], f1);
    SET_BIT(pos->bitboards[R], a1); SET_BIT(pos->bitboards[R], h1);
    SET_BIT(pos->bitboards[Q], d1);
    SET_BIT(pos->bitboards[n], b8); SET_BIT(pos->bitboards[n], g8);
    SET_BIT(pos->bitboards[b], c8); SET_BIT(pos->bitboards[b], f8);
    SET_BIT(pos->bitboards[r], a8); SET_BIT(pos->bitboards[r], h8);
    SET_BIT(pos->bitboards[q], d8);
}

static void test_king_prefers_home_over_center_in_middlegame(void)
{
    Position home, center;
    clear_position(&home); clear_position(&center);
    SET_BIT(home.bitboards[K], e1);   SET_BIT(home.bitboards[k], e5);
    SET_BIT(center.bitboards[K], e4); SET_BIT(center.bitboards[k], e5);
    add_full_material(&home); add_full_material(&center);
    home.side = WHITE; center.side = WHITE;
    update_occupancies(&home); update_occupancies(&center);

    check("middlegame: White king on e1 scores higher than on e4",
          evaluate(&home) > evaluate(&center));
}

static void test_castled_king_beats_random_square(void)
{
    Position castled, exposed;
    clear_position(&castled); clear_position(&exposed);
    SET_BIT(castled.bitboards[K], g1); SET_BIT(castled.bitboards[k], e5);
    SET_BIT(exposed.bitboards[K], a4); SET_BIT(exposed.bitboards[k], e5);
    add_full_material(&castled); add_full_material(&exposed);
    castled.side = WHITE; exposed.side = WHITE;
    update_occupancies(&castled); update_occupancies(&exposed);

    check("middlegame: castled king (g1) scores higher than a4",
          evaluate(&castled) > evaluate(&exposed));
}

static void test_pawn_prefers_advancing(void)
{
    Position start, advanced;
    clear_position(&start); clear_position(&advanced);
    SET_BIT(start.bitboards[K], e1);    SET_BIT(start.bitboards[k], e8);
    SET_BIT(start.bitboards[P], a2);
    SET_BIT(advanced.bitboards[K], e1); SET_BIT(advanced.bitboards[k], e8);
    SET_BIT(advanced.bitboards[P], a7);
    start.side = WHITE; advanced.side = WHITE;
    update_occupancies(&start); update_occupancies(&advanced);

    check("a pawn one step from promoting (a7) scores higher than on a2",
          evaluate(&advanced) > evaluate(&start));
}

static void test_rook_prefers_seventh_rank(void)
{
    Position second, seventh;
    clear_position(&second); clear_position(&seventh);
    SET_BIT(second.bitboards[K], e1);  SET_BIT(second.bitboards[k], e8);
    SET_BIT(second.bitboards[R], a2);
    SET_BIT(seventh.bitboards[K], e1); SET_BIT(seventh.bitboards[k], e8);
    SET_BIT(seventh.bitboards[R], a7);
    second.side = WHITE; seventh.side = WHITE;
    update_occupancies(&second); update_occupancies(&seventh);

    check("a rook on the 7th rank (a7) scores higher than on the 2nd (a2)",
          evaluate(&seventh) > evaluate(&second));
}

static void test_black_gets_the_same_treatment(void)
{
    Position home, center;
    clear_position(&home); clear_position(&center);
    SET_BIT(home.bitboards[k], e8);   SET_BIT(home.bitboards[K], e4);
    SET_BIT(center.bitboards[k], e5); SET_BIT(center.bitboards[K], e4);
    add_full_material(&home); add_full_material(&center);
    home.side = BLACK; center.side = BLACK;
    update_occupancies(&home); update_occupancies(&center);

    check("middlegame: Black king on e8 scores higher (for Black) than on e5",
          evaluate(&home) > evaluate(&center));
}

static void test_symmetric_position_is_exactly_zero(void)
{
    /* A perfectly mirrored position (standard start) must evaluate to
     * exactly 0 regardless of whose move it is -- side to move only
     * flips the sign of an already-symmetric material+PST sum. */
    Position pos;
    init_start_position(&pos);
    check("standard start evaluates to exactly 0",
          evaluate(&pos) == 0);
}

static void test_bare_king_endgame_prefers_centralization(void)
{
    /* With no other material (game_phase() == 0), the king should
     * want to centralize -- the opposite preference from the
     * middlegame table, and the whole reason the taper exists. */
    Position corner, center;
    clear_position(&corner); clear_position(&center);
    SET_BIT(corner.bitboards[K], a1); SET_BIT(corner.bitboards[k], a8);
    SET_BIT(center.bitboards[K], e4); SET_BIT(center.bitboards[k], a8);
    corner.side = WHITE; center.side = WHITE;
    update_occupancies(&corner); update_occupancies(&center);

    check("bare king endgame: centralized king (e4) beats a corner (a1)",
          evaluate(&center) > evaluate(&corner));
}

/* The same position with colours swapped and the board turned round. */
static void mirror_fen(const char *fen, char *out, size_t n)
{
    char board[128], side[4], castle[8], ep[4], rest[32] = "0 1";
    sscanf(fen, "%127s %3s %7s %3s %31[^\n]", board, side, castle, ep, rest);
    char *ranks[8], *save = NULL;
    int nr = 0;
    for (char *r = strtok_r(board, "/", &save); r && nr < 8; r = strtok_r(NULL, "/", &save)) ranks[nr++] = r;
    char b[128] = "";
    for (int i = nr - 1; i >= 0; i--) {
        for (char *c = ranks[i]; *c; c++) *c = isupper((unsigned char)*c) ? tolower(*c) : toupper(*c);
        strcat(b, ranks[i]);
        if (i) strcat(b, "/");
    }
    char c2[8] = "";
    if (strchr(castle, 'k')) strcat(c2, "K");
    if (strchr(castle, 'q')) strcat(c2, "Q");
    if (strchr(castle, 'K')) strcat(c2, "k");
    if (strchr(castle, 'Q')) strcat(c2, "q");
    if (!c2[0]) strcpy(c2, "-");
    if (ep[0] != '-') ep[1] = ep[1] == '3' ? '6' : '3';
    snprintf(out, n, "%s %s %s %s %s", b, side[0] == 'w' ? "b" : "w", c2, ep, rest);
}

static int eval_fen(const char *fen)
{
    Position pos;
    parse_fen(fen, &pos, NULL, NULL);
    return evaluate(&pos);
}

static void test_mirror_symmetry(void)
{
    printf("== colour symmetry, every term on ==\n");
    static const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 0 8",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbqkbnr/pp1ppppp/8/2pP4/8/8/PPP1PPPP/RNBQKBNR w KQkq c6 0 3",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "8/P7/8/8/8/8/7p/K6k w - - 0 1",
        "4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 b - - 0 1",
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 5 4",
        "8/8/3k4/8/8/4K3/8/8 w - - 0 1",
    };
    EvalOptions all = { 1, 1, 1, 1, 1 };
    eval_set_options(&all);
    EvalParams saved = *eval_params();
    const EvalParams *sets[] = { eval_default_params(), eval_tuned_params() };
    int ok = 1;
    for (int k = 0; k < 2; k++) {
        eval_set_params(sets[k]);
        for (size_t i = 0; i < sizeof(fens) / sizeof(fens[0]); i++) {
            char m[160];
            mirror_fen(fens[i], m, sizeof(m));
            int a = eval_fen(fens[i]), b = eval_fen(m);
            if (a != b) { ok = 0; printf("    %d vs %d: %s\n", a, b, fens[i]); }
        }
    }
    eval_set_params(&saved);
    check("a position and its mirror score the same, either weights", ok);
    EvalOptions none = { 0 };
    eval_set_options(&none);
}

/* What one term adds to White's side of the score. */
static int term(const char *fen, int which)
{
    EvalOptions o = { 0 }, none = { 0 };
    int *f[] = { &o.pesto, &o.pawns, &o.mobility, &o.king, &o.extras };
    *f[which] = 1;
    eval_set_options(&o);
    int with = eval_fen(fen);
    eval_set_options(&none);
    return with - eval_fen(fen);
}

static void test_terms(void)
{
    printf("== the terms ==\n");
    enum { PESTO, PAWNS, MOB, KING, XTRA };
    check("doubled, isolated pawns score below connected ones",
          term("8/8/8/8/8/1P6/1P6/k6K w - - 0 1", PAWNS) < term("8/8/8/8/8/2P5/1P6/k6K w - - 0 1", PAWNS));
    check("a passed pawn is worth more further up",
          term("k7/8/4P3/8/8/8/8/7K w - - 0 1", PAWNS) > term("k7/8/8/8/8/4P3/8/7K w - - 0 1", PAWNS));
    check("a central knight is more mobile than a cornered one",
          term("k7/8/8/3N4/8/8/8/7K w - - 0 1", MOB) > term("k7/8/8/8/8/8/8/N6K w - - 0 1", MOB));
    check("a shielded king scores above an exposed one (with queens on)",
          term("q5k1/8/8/8/8/8/5PPP/Q5K1 w - - 0 1", KING) > term("q5k1/8/8/8/8/8/8/Q5K1 w - - 0 1", KING));
    check("the bishop pair scores above bishop and knight",
          term("k7/8/8/8/8/8/8/2B1BK2 w - - 0 1", XTRA) > term("k7/8/8/8/8/8/8/2B1NK2 w - - 0 1", XTRA));
    check("PeSTO tables value a queen above a rook", term("k7/8/8/8/8/8/8/Q6K w - - 0 1", PESTO) >
                                                     term("k7/8/8/8/8/8/8/R6K w - - 0 1", PESTO) - 1000);
}

static void test_params_unchanged(void)
{
    printf("== the parameter table ==\n");
    static const char *f[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 0 8",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "2r2rk1/pp3ppp/2n5/3p4/3P4/2PB1N2/P4PPP/R4RK1 b - - 0 1",
        "8/5pk1/6p1/1PP5/8/6P1/5PK1/8 w - - 0 1",
        "r1b2rk1/ppq2ppp/2nbpn2/3p4/2PP4/1PN1PN2/PB2BPPP/R2QK2R w KQ - 0 1",
    };
    static const int before[] = { 0, 52, 3, 134, 3, -319, 335, 3 };   /* the hand-set weights */
    EvalOptions all = { 1, 1, 1, 1, 1 };
    eval_set_options(&all);
    EvalParams saved = *eval_params();
    eval_set_params(eval_default_params());
    int same = 1;
    for (int i = 0; i < 8; i++)
        if (eval_fen(f[i]) != before[i]) { same = 0; printf("    %d, was %d: %s\n", eval_fen(f[i]), before[i], f[i]); }
    check("the default parameters are the hand-set weights", same);
    eval_set_params(&saved);
    EvalOptions none = { 0 };
    eval_set_options(&none);
}

int main(void)
{
    init_attacks();

    printf("== piece-square table orientation (regression guard) ==\n");
    test_king_prefers_home_over_center_in_middlegame();
    test_castled_king_beats_random_square();
    test_pawn_prefers_advancing();
    test_rook_prefers_seventh_rank();
    test_black_gets_the_same_treatment();

    printf("== sanity/symmetry ==\n");
    test_symmetric_position_is_exactly_zero();

    printf("== king PST tapering ==\n");
    test_bare_king_endgame_prefers_centralization();
    test_mirror_symmetry();
    test_params_unchanged();
    test_terms();

    printf("\n%s\n", failures == 0 ? "All eval tests passed."
                                   : "EVAL TEST FAILURES DETECTED.");
    return failures == 0 ? 0 : 1;
}
