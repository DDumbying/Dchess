#ifndef TEST_COMMON_H
#define TEST_COMMON_H

/* Small shared helper for building known test positions without a FEN
 * parser (this codebase doesn't have one). `board` is 64 chars, one per
 * square, indexed the same way as the engine's square enum: index 0 is
 * a1, index 7 is h1, index 8 is a2, ... index 63 is h8. Use '.' for an
 * empty square and the usual FEN piece letters (upper = white) for the
 * rest. Writing it out rank-by-rank from rank 1 to rank 8 (i.e. the
 * reverse of how you'd read a FEN string, which goes rank 8 to rank 1)
 * lines it up visually with the board. */
#include "engine/board.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

static inline void setup_position(Position *pos, const char board[64],
                                   int side, int castling, int enpassant)
{
    clear_position(pos);
    for (int sq = 0; sq < 64; sq++) {
        int piece;
        switch (board[sq]) {
            case 'P': piece = P; break;
            case 'N': piece = N; break;
            case 'B': piece = B; break;
            case 'R': piece = R; break;
            case 'Q': piece = Q; break;
            case 'K': piece = K; break;
            case 'p': piece = p; break;
            case 'n': piece = n; break;
            case 'b': piece = b; break;
            case 'r': piece = r; break;
            case 'q': piece = q; break;
            case 'k': piece = k; break;
            default: continue; /* '.' or anything else: empty square */
        }
        SET_BIT(pos->bitboards[piece], sq);
    }
    pos->side      = side;
    pos->castling  = castling;
    pos->enpassant = enpassant;
    update_occupancies(pos);
}

/* Startpos — the standard opening array. */
#define TESTPOS_START \
    "RNBQKBNR" "PPPPPPPP" "........" "........" \
    "........" "........" "pppppppp" "rnbqkbnr"

/* "Kiwipete" (Steven Edwards) — the classic movegen torture-test
 * position: exercises castling (both sides, both wings), en passant,
 * promotions, and pinned pieces all at once.
 * FEN: r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1 */
#define TESTPOS_KIWIPETE \
    "R...K..R" "PPPBBPPP" "..N..Q.p" ".p..P..." \
    "...PN..." "bn..pnp." "p.ppqpb." "r...k..r"

/* Promotion-heavy position (CPW "Position 5"): a white pawn one step
 * from promoting on d7, right next to the black king.
 * FEN: rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8 */
#define TESTPOS_PROMOTION \
    "RNBQK..R" "PPP.NnPP" "........" "..B....." \
    "........" "..p....." "pp.Pbppp" "rnbq.k.r"

#endif
