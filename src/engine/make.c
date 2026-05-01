#include "engine/make.h"
#include "engine/movegen.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include <string.h>

/* Castle right masks: when a square is touched, revoke these rights */
static const int castling_rights[64] = {
    [a1] = ~CASTLE_WHITE_QUEEN,
    [h1] = ~CASTLE_WHITE_KING,
    [e1] = ~(CASTLE_WHITE_KING|CASTLE_WHITE_QUEEN),
    [a8] = ~CASTLE_BLACK_QUEEN,
    [h8] = ~CASTLE_BLACK_KING,
    [e8] = ~(CASTLE_BLACK_KING|CASTLE_BLACK_QUEEN),
};

static void init_rights(int rights[64]) {
    for (int i = 0; i < 64; i++) rights[i] = ~0;
    rights[a1] = ~CASTLE_WHITE_QUEEN;
    rights[h1] = ~CASTLE_WHITE_KING;
    rights[e1] = ~(CASTLE_WHITE_KING|CASTLE_WHITE_QUEEN);
    rights[a8] = ~CASTLE_BLACK_QUEEN;
    rights[h8] = ~CASTLE_BLACK_KING;
    rights[e8] = ~(CASTLE_BLACK_KING|CASTLE_BLACK_QUEEN);
}

static int cr[64];
static int cr_initialized = 0;

int make_move(Position *pos, Move move) {
    if (!cr_initialized) { init_rights(cr); cr_initialized = 1; }

    int from  = FROM(move);
    int to    = TO(move);
    int piece = PIECE(move);
    int flags = FLAGS(move);
    int side  = pos->side;

    /* Remove piece from origin */
    CLEAR_BIT(pos->bitboards[piece], from);
    SET_BIT(pos->bitboards[piece], to);

    pos->enpassant = NO_SQ;

    /* Handle captures */
    if (flags & FLAG_CAPTURE) {
        if (flags & FLAG_ENPASSANT) {
            int cap_sq = (side == WHITE) ? to - 8 : to + 8;
            CLEAR_BIT(pos->bitboards[(side == WHITE) ? p : P], cap_sq);
        } else {
            /* Remove captured piece */
            int start = (side == WHITE) ? 6 : 0;
            int end   = (side == WHITE) ? 12 : 6;
            for (int i = start; i < end; i++) {
                if (GET_BIT(pos->bitboards[i], to)) {
                    CLEAR_BIT(pos->bitboards[i], to);
                    break;
                }
            }
            /* Re-place our piece (was cleared by loop if same color, impossible but safe) */
            SET_BIT(pos->bitboards[piece], to);
        }
    }

    /* Double pawn push: set en passant square */
    if (flags & FLAG_DOUBLE_PUSH) {
        pos->enpassant = (side == WHITE) ? to - 8 : to + 8;
    }

    /* Promotions */
    if (flags & FLAG_PROMOTION) {
        CLEAR_BIT(pos->bitboards[piece], to);
        if (flags & FLAG_PROMO_Q)      SET_BIT(pos->bitboards[(side==WHITE)?Q:q], to);
        else if (flags & FLAG_PROMO_R) SET_BIT(pos->bitboards[(side==WHITE)?R:r], to);
        else if (flags & FLAG_PROMO_B) SET_BIT(pos->bitboards[(side==WHITE)?B:b], to);
        else if (flags & FLAG_PROMO_N) SET_BIT(pos->bitboards[(side==WHITE)?N:n], to);
    }

    /* Castling: move rook */
    if (flags & FLAG_CASTLING) {
        if (to == g1) { CLEAR_BIT(pos->bitboards[R],h1); SET_BIT(pos->bitboards[R],f1); }
        if (to == c1) { CLEAR_BIT(pos->bitboards[R],a1); SET_BIT(pos->bitboards[R],d1); }
        if (to == g8) { CLEAR_BIT(pos->bitboards[r],h8); SET_BIT(pos->bitboards[r],f8); }
        if (to == c8) { CLEAR_BIT(pos->bitboards[r],a8); SET_BIT(pos->bitboards[r],d8); }
    }

    /* Update castling rights */
    pos->castling &= cr[from];
    pos->castling &= cr[to];

    /* Switch side */
    pos->side ^= 1;

    update_occupancies(pos);

    /* Check legality: was our king left in check? */
    if (is_in_check(pos, side)) {
        return 0; /* illegal */
    }
    return 1;
}

void undo_move(Position *pos, Move move, const Position *saved) {
    (void)move;
    memcpy(pos, saved, sizeof(Position));
}
