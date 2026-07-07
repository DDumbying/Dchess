#include "engine/eval.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

/* Material values in centipawns */
static const int piece_value[12] = {
    100, 320, 330, 500, 900, 20000,   /* white */
    100, 320, 330, 500, 900, 20000    /* black */
};

/* Piece-square tables (white perspective, a1=0) */
static const int pawn_pst[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
static const int knight_pst[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};
static const int bishop_pst[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};
static const int rook_pst[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};
static const int queen_pst[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};
static const int king_pst[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

/* Endgame king PST: the middlegame table above keeps the king tucked
 * in the corner (correct while there's enough material on the board to
 * mate it), but that's the wrong instinct once most pieces are traded
 * off -- an endgame king wants to walk toward the center, where it can
 * support its own pawns and attack the opponent's. Standard values (the
 * same shape used by PeSTO and similar simple tapered evaluations). */
static const int king_endgame_pst[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

/* ── Game phase ──────────────────────────────────────────────────────────
 * A simple material-based phase count (the same scheme popularized by
 * PeSTO): each non-pawn, non-king piece contributes a weight, and the
 * total tells us where we are between "everyone's still on the board"
 * (phase == MAX_PHASE, pure middlegame) and "mostly traded off" (phase
 * == 0, pure endgame). Only the king's PST is tapered by this for now
 * -- see king_endgame_pst above -- rather than every piece, keeping
 * this a targeted fix for the specific gap it addresses instead of a
 * full PeSTO-style rewrite of the whole evaluation. */
static const int phase_weight[6] = { 0, 1, 1, 2, 4, 0 }; /* P,N,B,R,Q,K */
#define MAX_PHASE 24 /* 2 sides * (2N+2B+2R+1Q worth: 2+2+4+4 = 12) */

static int game_phase(const Position *pos) {
    int phase = 0;
    for (int piece = 0; piece < 12; piece++)
        phase += phase_weight[piece % 6] * count_bits(pos->bitboards[piece]);
    return phase > MAX_PHASE ? MAX_PHASE : phase; /* promotions can exceed the nominal max */
}

static const int *pst[12] = {
    pawn_pst, knight_pst, bishop_pst, rook_pst, queen_pst, king_pst,
    pawn_pst, knight_pst, bishop_pst, rook_pst, queen_pst, king_pst
};

/* Mirror for black (flip rank) */
static int mirror(int sq) { return (7-(sq/8))*8 + (sq%8); }

int evaluate(const Position *pos) {
    int phase = game_phase(pos);
    int score = 0;
    for (int piece = 0; piece < 12; piece++) {
        U64 bb = pos->bitboards[piece];
        int is_white = piece < 6;
        int type = piece % 6;
        while (bb) {
            int sq = pop_lsb(&bb);
            /* NOTE: king_pst/pawn_pst/etc. above are written in the
             * standard rank-8-first convention (row 0 = rank 8, row 7 =
             * rank 1) used by every published reference for these exact
             * values -- not in this engine's native a1=0 square order.
             * So it's WHITE that needs mirror() here (to convert its
             * native numbering into the table's layout); Black's own
             * native sq already lines up directly. (Verified empirically
             * -- see the eval.c bugfix note in docs/overview.md.) */
            int table_sq = is_white ? mirror(sq) : sq;
            int pst_val;
            if (type == 5) {
                /* King: blend the middlegame and endgame tables by phase.
                 * At phase == MAX_PHASE (full material) this collapses to
                 * king_pst[] exactly, matching the previous behavior; it
                 * shifts toward king_endgame_pst[] as material comes off. */
                int mg = king_pst[table_sq];
                int eg = king_endgame_pst[table_sq];
                pst_val = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;
            } else {
                pst_val = pst[type][table_sq];
            }
            int val = piece_value[piece];
            if (is_white) score += val + pst_val;
            else          score -= val + pst_val;
        }
    }
    return (pos->side == WHITE) ? score : -score;
}
