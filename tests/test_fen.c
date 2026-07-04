/* FEN parser/generator tests.
 *
 * Build & run:
 *   gcc -Iheaders -O2 tests/test_fen.c src/engine/[a-z]*.c src/utils/bitboard.c -o /tmp/test_fen
 *   /tmp/test_fen
 */
#include <stdio.h>
#include <string.h>
#include "engine/board.h"
#include "engine/fen.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include "test_common.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static int positions_equal(const Position *a, const Position *b)
{
    for (int i = 0; i < 12; i++)
        if (a->bitboards[i] != b->bitboards[i]) return 0;
    return a->side == b->side &&
           a->castling == b->castling &&
           a->enpassant == b->enpassant;
}

static void test_startpos_matches_init(void)
{
    Position from_fen, from_init;
    int hm = -1, fm = -1;
    int ok = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                        &from_fen, &hm, &fm);
    init_start_position(&from_init);

    check("startpos FEN parses successfully", ok);
    check("startpos FEN matches init_start_position()",
          ok && positions_equal(&from_fen, &from_init));
    check("startpos FEN: halfmove clock = 0", hm == 0);
    check("startpos FEN: fullmove number = 1", fm == 1);
}

static void test_kiwipete_matches_mailbox(void)
{
    /* Independent cross-check: build Kiwipete two different ways (the
     * mailbox helper used by perft.c/test_movegen.c, and a real FEN
     * string) and confirm they produce byte-identical positions. */
    Position from_fen, from_mailbox;
    int ok = parse_fen(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        &from_fen, NULL, NULL);
    setup_position(&from_mailbox, TESTPOS_KIWIPETE, WHITE,
                   CASTLE_WHITE_KING|CASTLE_WHITE_QUEEN|CASTLE_BLACK_KING|CASTLE_BLACK_QUEEN,
                   NO_SQ);

    check("kiwipete FEN parses successfully", ok);
    check("kiwipete FEN matches independent mailbox construction",
          ok && positions_equal(&from_fen, &from_mailbox));
}

static void test_en_passant_field(void)
{
    Position pos;
    int ok = parse_fen(
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
        &pos, NULL, NULL);
    check("FEN with active en passant square parses", ok);
    check("en passant square decoded as d6", ok && pos.enpassant == d6);
}

static void test_missing_clocks_default(void)
{
    /* Some puzzle sites omit the halfmove/fullmove fields entirely. */
    Position pos;
    int hm = -1, fm = -1;
    int ok = parse_fen("8/8/8/4k3/8/8/8/4K2R w K - ", &pos, &hm, &fm);
    check("FEN with missing clocks still parses", ok);
    check("missing halfmove clock defaults to 0", hm == 0);
    check("missing fullmove number defaults to 1", fm == 1);
}

static void test_round_trip(void)
{
    const char *original = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    Position pos;
    int hm, fm;
    parse_fen(original, &pos, &hm, &fm);

    char regenerated[FEN_BUFSIZE];
    position_to_fen(&pos, hm, fm, regenerated, sizeof(regenerated));

    check("round-trip: parse -> generate reproduces the original FEN",
          strcmp(original, regenerated) == 0);
}

static void test_malformed_fen_rejected_and_pos_untouched(void)
{
    const char *bad_fens[] = {
        "not a fen at all",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBX w KQkq - 0 1",     /* bad piece char 'X' */
        "rnbqkbnr/pppppppp/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",      /* only 7 ranks */
        "rnbqkbnr/ppppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",   /* rank sums to 9 */
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",    /* bad side char */
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w ZZZZ - 0 1",    /* bad castling char */
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq z9 0 1",   /* bad en passant square */
    };
    int n = (int)(sizeof(bad_fens) / sizeof(bad_fens[0]));

    for (int i = 0; i < n; i++) {
        Position sentinel;
        init_start_position(&sentinel);
        Position before = sentinel;

        int ok = parse_fen(bad_fens[i], &sentinel, NULL, NULL);
        char label[80];
        snprintf(label, sizeof(label), "malformed FEN #%d rejected", i);
        check(label, !ok);

        snprintf(label, sizeof(label), "malformed FEN #%d leaves position untouched on failure", i);
        check(label, positions_equal(&sentinel, &before));
    }
}

int main(void)
{
    init_attacks();

    printf("== round trip against known-good constructions ==\n");
    test_startpos_matches_init();
    test_kiwipete_matches_mailbox();
    test_round_trip();

    printf("== optional/special fields ==\n");
    test_en_passant_field();
    test_missing_clocks_default();

    printf("== rejects malformed input safely ==\n");
    test_malformed_fen_rejected_and_pos_untouched();

    printf("\n%s\n", failures == 0 ? "All FEN tests passed."
                                   : "FEN TEST FAILURES DETECTED.");
    return failures == 0 ? 0 : 1;
}
