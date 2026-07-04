/* perft: the standard chess movegen correctness test. It exhaustively
 * counts leaf nodes reached by playing every legal move to a fixed
 * depth from a known position, and compares against published
 * known-correct counts. A mismatch means move generation, make_move,
 * or the legality check has a bug -- the exact move that's wrong isn't
 * pinpointed, but knowing *a* bug exists is the important first step.
 *
 * Build & run:
 *   gcc -Iheaders -O2 tests/perft.c src/engine/[a-z]*.c src/utils/bitboard.c \
 *       -o /tmp/perft && /tmp/perft
 *
 * Known-correct counts are the standard values from the Chess
 * Programming Wiki's perft results page.
 */
#include <stdio.h>
#include <string.h>
#include "engine/board.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include "test_common.h"

static long perft(Position *pos, int depth)
{
    if (depth == 0) return 1;

    MoveList ml;
    generate_moves(pos, &ml);

    long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Position saved = *pos;
        if (make_move(pos, ml.moves[i]))
            nodes += perft(pos, depth - 1);
        *pos = saved;
    }
    return nodes;
}

typedef struct {
    const char *name;
    const char  board[65]; /* 64 squares + implicit NUL from string literal */
    int side, castling, enpassant;
    int max_depth;
    long expected[6]; /* depth 1..max_depth, index 0 unused */
} PerftCase;

int main(void)
{
    init_attacks();

    PerftCase cases[] = {
        {
            "startpos", TESTPOS_START, WHITE,
            CASTLE_WHITE_KING|CASTLE_WHITE_QUEEN|CASTLE_BLACK_KING|CASTLE_BLACK_QUEEN,
            NO_SQ, 5,
            { 0, 20, 400, 8902, 197281, 4865609 }
        },
        {
            "kiwipete (castling/en-passant/pins torture test)", TESTPOS_KIWIPETE, WHITE,
            CASTLE_WHITE_KING|CASTLE_WHITE_QUEEN|CASTLE_BLACK_KING|CASTLE_BLACK_QUEEN,
            NO_SQ, 4,
            { 0, 48, 2039, 97862, 4085603, 0 }
        },
        {
            "promotion-heavy position", TESTPOS_PROMOTION, WHITE,
            CASTLE_WHITE_KING|CASTLE_WHITE_QUEEN,
            NO_SQ, 4,
            { 0, 44, 1486, 62379, 2103487, 0 }
        },
    };
    int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    int failures = 0;
    for (int c = 0; c < n_cases; c++) {
        PerftCase *tc = &cases[c];
        printf("== %s ==\n", tc->name);
        for (int depth = 1; depth <= tc->max_depth; depth++) {
            Position pos;
            setup_position(&pos, tc->board, tc->side, tc->castling, tc->enpassant);
            long got = perft(&pos, depth);
            long want = tc->expected[depth];
            int ok = (got == want);
            printf("  depth %d: got %-10ld expected %-10ld %s\n",
                   depth, got, want, ok ? "OK" : "FAIL");
            if (!ok) failures++;
        }
    }

    printf("\n%s\n", failures == 0 ? "All perft checks passed."
                                    : "PERFT FAILURES DETECTED -- move generation has a bug.");
    return failures == 0 ? 0 : 1;
}
