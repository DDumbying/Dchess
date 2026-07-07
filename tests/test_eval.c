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
#include <stdio.h>
#include "engine/board.h"
#include "engine/eval.h"
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

    printf("\n%s\n", failures == 0 ? "All eval tests passed."
                                   : "EVAL TEST FAILURES DETECTED.");
    return failures == 0 ? 0 : 1;
}
