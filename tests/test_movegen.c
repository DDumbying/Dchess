/* Small, targeted move-generation unit tests -- complements perft.c's
 * aggregate node counts with checks that specific rules (en passant,
 * promotion, checkmate/stalemate detection) work as expected and are
 * easy to read/debug individually.
 *
 * Build & run (from the repo root):
 *   gcc -Iheaders -O2 tests/test_movegen.c src/engine/[a-z]*.c src/utils/bitboard.c -o /tmp/test_movegen
 *   /tmp/test_movegen
 */
#include <stdio.h>
#include <string.h>
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
    printf("  %-45s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Count only the moves that survive the legality check (king not left
 * in check), matching what a caller actually gets to choose between. */
static int count_legal(Position *pos)
{
    MoveList ml;
    generate_moves(pos, &ml);
    int legal = 0;
    for (int i = 0; i < ml.count; i++) {
        Position saved = *pos;
        if (make_move(pos, ml.moves[i])) legal++;
        *pos = saved;
    }
    return legal;
}

static void test_startpos_move_count(void)
{
    Position pos;
    init_start_position(&pos);
    check("startpos has exactly 20 legal moves", count_legal(&pos) == 20);
}

static void test_checkmate_detected(void)
{
    /* Fool's mate: 1.f3 e5 2.g4 Qh4# -- fastest checkmate in chess.
     * White to move, in check, with zero legal replies. */
    const char board[64] =
        "RNBQKBNR" "PPPPP..P" ".....P.." "......Pq"
        "....p..." "........" "pppp.ppp" "rnb.kbnr";
    Position pos;
    setup_position(&pos, board, WHITE,
                   CASTLE_WHITE_KING|CASTLE_WHITE_QUEEN|CASTLE_BLACK_KING|CASTLE_BLACK_QUEEN,
                   NO_SQ);
    check("fool's mate: white is in check", is_in_check(&pos, WHITE));
    check("fool's mate: white has no legal moves", count_legal(&pos) == 0);
}

static void test_stalemate_detected(void)
{
    /* Classic stalemate study: black king boxed into a8 by white king
     * on b6 and white queen on c7, black to move, not in check, and
     * with zero legal moves available. */
    Position pos;
    clear_position(&pos);
    SET_BIT(pos.bitboards[k], a8);
    SET_BIT(pos.bitboards[K], b6);
    SET_BIT(pos.bitboards[Q], c7);
    pos.side = BLACK;
    pos.castling = 0;
    pos.enpassant = NO_SQ;
    update_occupancies(&pos);

    check("stalemate: black is not in check", !is_in_check(&pos, BLACK));
    check("stalemate: black has no legal moves", count_legal(&pos) == 0);
}

static void test_en_passant_capture(void)
{
    /* White pawn just double-pushed d2-d4; black pawn on e4 can capture
     * en passant to d3, removing the white pawn from d4. */
    Position pos;
    clear_position(&pos);
    SET_BIT(pos.bitboards[K], e1);
    SET_BIT(pos.bitboards[k], e8);
    SET_BIT(pos.bitboards[P], d4);
    SET_BIT(pos.bitboards[p], e4);
    pos.side = BLACK;
    pos.castling = 0;
    pos.enpassant = d3;
    update_occupancies(&pos);

    MoveList ml;
    generate_moves(&pos, &ml);

    int found = 0;
    for (int i = 0; i < ml.count; i++) {
        if (FROM(ml.moves[i]) == e4 && TO(ml.moves[i]) == d3 &&
            (FLAGS(ml.moves[i]) & FLAG_ENPASSANT)) {
            found = 1;
            Position after = pos;
            make_move(&after, ml.moves[i]);
            check("en passant: capturing pawn lands on d3",
                  GET_BIT(after.bitboards[p], d3) != 0);
            check("en passant: captured pawn removed from d4",
                  !GET_BIT(after.bitboards[P], d4) &&
                  !GET_BIT(after.occupancies[BOTH], d4));
        }
    }
    check("en passant: e4xd3 e.p. move is generated", found);
}

static void test_promotion_generates_all_four_pieces(void)
{
    /* White pawn on a7, nothing on a8: a7-a8 must generate 4 promotion
     * moves (queen, rook, bishop, knight), not just one. */
    Position pos;
    clear_position(&pos);
    SET_BIT(pos.bitboards[K], e1);
    SET_BIT(pos.bitboards[k], e8);
    SET_BIT(pos.bitboards[P], a7);
    pos.side = WHITE;
    pos.castling = 0;
    pos.enpassant = NO_SQ;
    update_occupancies(&pos);

    MoveList ml;
    generate_moves(&pos, &ml);

    int seen = 0;
    for (int i = 0; i < ml.count; i++) {
        if (FROM(ml.moves[i]) == a7 && TO(ml.moves[i]) == a8)
            seen |= (FLAGS(ml.moves[i]) & FLAG_PROMOTION);
    }
    check("promotion: queen promotion generated", (seen & FLAG_PROMO_Q) != 0);
    check("promotion: rook promotion generated",  (seen & FLAG_PROMO_R) != 0);
    check("promotion: bishop promotion generated", (seen & FLAG_PROMO_B) != 0);
    check("promotion: knight promotion generated", (seen & FLAG_PROMO_N) != 0);
}

static void test_castling_blocked_through_check(void)
{
    /* White king e1, rook h1, rights intact, path empty -- but a black
     * rook on f8 attacks f1, the square the king passes through. O-O
     * must NOT be offered. */
    Position pos;
    clear_position(&pos);
    SET_BIT(pos.bitboards[K], e1);
    SET_BIT(pos.bitboards[R], h1);
    SET_BIT(pos.bitboards[k], e8);
    SET_BIT(pos.bitboards[r], f8);
    pos.side = WHITE;
    pos.castling = CASTLE_WHITE_KING;
    pos.enpassant = NO_SQ;
    update_occupancies(&pos);

    MoveList ml;
    generate_moves(&pos, &ml);
    int found = 0;
    for (int i = 0; i < ml.count; i++)
        if (FLAGS(ml.moves[i]) & FLAG_CASTLING) found = 1;

    check("castling: O-O withheld when king would pass through check", !found);
}

int main(void)
{
    init_attacks();

    printf("== basic move counts ==\n");
    test_startpos_move_count();

    printf("== game-end detection ==\n");
    test_checkmate_detected();
    test_stalemate_detected();

    printf("== special moves ==\n");
    test_en_passant_capture();
    test_promotion_generates_all_four_pieces();
    test_castling_blocked_through_check();

    printf("\n%s\n", failures == 0 ? "All move-generation unit tests passed."
                                   : "MOVE-GENERATION TEST FAILURES DETECTED.");
    return failures == 0 ? 0 : 1;
}
