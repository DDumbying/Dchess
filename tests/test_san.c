/* SAN generation tests.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/san.h"
#include "engine/board.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include "test_common.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Find the legal move from->to (optionally a specific promotion) and
 * render it. Writes "?" if no such move exists, so a broken test setup
 * shows up as a mismatch rather than as a pass. */
static void san_of(const Position *pos, int from, int to, int promo, char *out)
{
    MoveList ml;
    generate_moves(pos, &ml);
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;
        if (promo && !(FLAGS(m) & promo)) continue;
        if (!promo && (FLAGS(m) & FLAG_PROMOTION) && !(FLAGS(m) & FLAG_PROMO_Q)) continue;
        Position test = *pos;
        if (!make_move(&test, m)) continue;
        san_write(pos, m, out);
        return;
    }
    strcpy(out, "?");
}

static void expect(const Position *pos, int from, int to, int promo,
                   const char *want)
{
    char got[SAN_MAXLEN];
    san_of(pos, from, to, promo, got);
    char label[96];
    snprintf(label, sizeof(label), "%s", want);
    check(label, strcmp(got, want) == 0);
    if (strcmp(got, want) != 0)
        printf("        expected \"%s\", got \"%s\"\n", want, got);
}

/* NB: the 8 groups run rank 1 FIRST, so group N is rank N+1 -- the
 * reverse of how a FEN reads. */
static void board(Position *p, const char b[64], int side, int castling, int ep)
{
    setup_position(p, b, side, castling, ep);
}

static void test_pawns(void)
{
    printf("== pawns ==\n");
    Position p;

    board(&p, TESTPOS_START, WHITE, 0, -1);
    expect(&p, e2, e4, 0, "e4");
    expect(&p, g1, f3, 0, "Nf3");

    /* Capture uses the departing file. */
    board(&p, "K......." "........" "........" "....P..."
              "...p...." "........" "........" ".......k",
          WHITE, 0, -1);
    expect(&p, e4, d5, 0, "exd5");

    /* En passant reads like any other pawn capture. */
    board(&p, "K......." "........" "........" "........"
              "...pP..." "........" "........" ".......k",
          WHITE, 0, d6);
    expect(&p, e5, d6, 0, "exd6");

    /* Promotion, quiet and capturing. */
    /* Black king off the eighth rank, so promoting is not also a check. */
    board(&p, "K......." "........" "........" "........"
              ".......k" "........" ".P......" "........",
          WHITE, 0, -1);
    expect(&p, b7, b8, FLAG_PROMO_Q, "b8=Q");
    expect(&p, b7, b8, FLAG_PROMO_N, "b8=N");

    board(&p, "K......." "........" "........" "........"
              ".......k" "........" ".P......" "..r.....",
          WHITE, 0, -1);
    expect(&p, b7, c8, FLAG_PROMO_Q, "bxc8=Q");

    /* And one that does check, since the suffix is part of the notation. */
    board(&p, "K......." "........" "........" "........"
              "........" "........" ".P......" ".......k",
          WHITE, 0, -1);
    expect(&p, b7, b8, FLAG_PROMO_Q, "b8=Q+");
}

static void test_pieces_and_castling(void)
{
    printf("== pieces and castling ==\n");
    Position p;

    board(&p, "K......." "........" ".....N.." "........"
              "........" "........" "........" ".......k",
          WHITE, 0, -1);
    expect(&p, f3, e5, 0, "Ne5");

    board(&p, "K......." "........" ".....N.." "........"
              "....p..." "........" "........" ".......k",
          WHITE, 0, -1);
    expect(&p, f3, e5, 0, "Nxe5");

    board(&p, "R...K..R" "........" "........" "........"
              "........" "........" "........" "....k...",
          WHITE, CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN, -1);
    expect(&p, e1, g1, 0, "O-O");
    expect(&p, e1, c1, 0, "O-O-O");
}

static void test_disambiguation(void)
{
    printf("== disambiguation ==\n");
    Position p;

    /* Two knights on b1 and f3 both reach d2 -- files differ. */
    board(&p, ".N..K..." "........" ".....N.." "........"
              "........" "........" "........" ".......k",
          WHITE, 0, -1);
    expect(&p, b1, d2, 0, "Nbd2");
    expect(&p, f3, d2, 0, "Nfd2");

    /* Two rooks on the same file, a1 and a5, both reach a3: ranks differ. */
    board(&p, "R...K..." "........" "........" "........"
              "R......." "........" "........" ".......k",
          WHITE, 0, -1);
    expect(&p, a1, a3, 0, "R1a3");
    expect(&p, a5, a3, 0, "R5a3");

    /* Qh4 shares its file with Qh1 and its rank with Qe4, so neither
     * alone identifies it and the full square is required. */
    board(&p, "K......Q" "........" "........" "....Q..Q"
              "........" "k......." "........" "........",
          WHITE, 0, -1);
    expect(&p, h4, e1, 0, "Qh4e1");

    /* A single piece never needs disambiguation. */
    board(&p, "K......." "........" ".....N.." "........"
              "........" "........" "........" ".......k",
          WHITE, 0, -1);
    expect(&p, f3, d2, 0, "Nd2");
}

static void test_check_and_mate(void)
{
    printf("== check and mate ==\n");
    Position p;

    /* Rook swings to the back rank and checks. */
    board(&p, "K......." "........" "........" "........"
              "........" "........" "R......." ".......k",
          WHITE, 0, -1);
    expect(&p, a7, h7, 0, "Rh7+");

    /* Back-rank mate: Ra8#, black king boxed in by its own pawns. */
    board(&p, "R......K" "........" "........" "........"
              "........" "........" ".....ppp" "......k.",
          WHITE, 0, -1);
    expect(&p, a1, a8, 0, "Ra8#");
}

int main(void)
{
    init_attacks();

    test_pawns();
    test_pieces_and_castling();
    test_disambiguation();
    test_check_and_mate();

    if (failures) {
        printf("\n%d SAN test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll SAN tests passed.\n");
    return 0;
}
