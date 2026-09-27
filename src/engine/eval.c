#include "engine/eval.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

/* Material values in centipawns */
static const int piece_value[12] = {
    100, 320, 330, 500, 900, 20000,   /* white */
    100, 320, 330, 500, 900, 20000    /* black */
};

/* CAREFUL: written rank 8 FIRST, so index 0 is a8 -- the opposite of the
 * engine's a1=0 numbering. mirror() at the lookup site reconciles them,
 * which is why it belongs on WHITE, not Black. */
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

/* Game phase
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

/* PeSTO (Rofchade's values, from the Chess Programming Wiki), written
 * rank 8 first like the tables above. */
static const int pesto_mg_value[6] = { 82, 337, 365, 477, 1025, 0 };
static const int pesto_eg_value[6] = { 94, 281, 297, 512, 936, 0 };
static const int pesto_mg_pawn_table[64] = {
       0,    0,    0,    0,    0,    0,    0,    0,
      98,  134,   61,   95,   68,  126,   34,  -11,
      -6,    7,   26,   31,   65,   56,   25,  -20,
     -14,   13,    6,   21,   23,   12,   17,  -23,
     -27,   -2,   -5,   12,   17,    6,   10,  -25,
     -26,   -4,   -4,  -10,    3,    3,   33,  -12,
     -35,   -1,  -20,  -23,  -15,   24,   38,  -22,
       0,    0,    0,    0,    0,    0,    0,    0,
};
static const int pesto_mg_knight_table[64] = {
    -167,  -89,  -34,  -49,   61,  -97,  -15, -107,
     -73,  -41,   72,   36,   23,   62,    7,  -17,
     -47,   60,   37,   65,   84,  129,   73,   44,
      -9,   17,   19,   53,   37,   69,   18,   22,
     -13,    4,   16,   13,   28,   19,   21,   -8,
     -23,   -9,   12,   10,   19,   17,   25,  -16,
     -29,  -53,  -12,   -3,   -1,   18,  -14,  -19,
    -105,  -21,  -58,  -33,  -17,  -28,  -19,  -23,
};
static const int pesto_mg_bishop_table[64] = {
     -29,    4,  -82,  -37,  -25,  -42,    7,   -8,
     -26,   16,  -18,  -13,   30,   59,   18,  -47,
     -16,   37,   43,   40,   35,   50,   37,   -2,
      -4,    5,   19,   50,   37,   37,    7,   -2,
      -6,   13,   13,   26,   34,   12,   10,    4,
       0,   15,   15,   15,   14,   27,   18,   10,
       4,   15,   16,    0,    7,   21,   33,    1,
     -33,   -3,  -14,  -21,  -13,  -12,  -39,  -21,
};
static const int pesto_mg_rook_table[64] = {
      32,   42,   32,   51,   63,    9,   31,   43,
      27,   32,   58,   62,   80,   67,   26,   44,
      -5,   19,   26,   36,   17,   45,   61,   16,
     -24,  -11,    7,   26,   24,   35,   -8,  -20,
     -36,  -26,  -12,   -1,    9,   -7,    6,  -23,
     -45,  -25,  -16,  -17,    3,    0,   -5,  -33,
     -44,  -16,  -20,   -9,   -1,   11,   -6,  -71,
     -19,  -13,    1,   17,   16,    7,  -37,  -26,
};
static const int pesto_mg_queen_table[64] = {
     -28,    0,   29,   12,   59,   44,   43,   45,
     -24,  -39,   -5,    1,  -16,   57,   28,   54,
     -13,  -17,    7,    8,   29,   56,   47,   57,
     -27,  -27,  -16,  -16,   -1,   17,   -2,    1,
      -9,  -26,   -9,  -10,   -2,   -4,    3,   -3,
     -14,    2,  -11,   -2,   -5,    2,   14,    5,
     -35,   -8,   11,    2,    8,   15,   -3,    1,
      -1,  -18,   -9,   10,  -15,  -25,  -31,  -50,
};
static const int pesto_mg_king_table[64] = {
     -65,   23,   16,  -15,  -56,  -34,    2,   13,
      29,   -1,  -20,   -7,   -8,   -4,  -38,  -29,
      -9,   24,    2,  -16,  -20,    6,   22,  -22,
     -17,  -20,  -12,  -27,  -30,  -25,  -14,  -36,
     -49,   -1,  -27,  -39,  -46,  -44,  -33,  -51,
     -14,  -14,  -22,  -46,  -44,  -30,  -15,  -27,
       1,    7,   -8,  -64,  -43,  -16,    9,    8,
     -15,   36,   12,  -54,    8,  -28,   24,   14,
};
static const int pesto_eg_pawn_table[64] = {
       0,    0,    0,    0,    0,    0,    0,    0,
     178,  173,  158,  134,  147,  132,  165,  187,
      94,  100,   85,   67,   56,   53,   82,   84,
      32,   24,   13,    5,   -2,    4,   17,   17,
      13,    9,   -3,   -7,   -7,   -8,    3,   -1,
       4,    7,   -6,    1,    0,   -5,   -1,   -8,
      13,    8,    8,   10,   13,    0,    2,   -7,
       0,    0,    0,    0,    0,    0,    0,    0,
};
static const int pesto_eg_knight_table[64] = {
     -58,  -38,  -13,  -28,  -31,  -27,  -63,  -99,
     -25,   -8,  -25,   -2,   -9,  -25,  -24,  -52,
     -24,  -20,   10,    9,   -1,   -9,  -19,  -41,
     -17,    3,   22,   22,   22,   11,    8,  -18,
     -18,   -6,   16,   25,   16,   17,    4,  -18,
     -23,   -3,   -1,   15,   10,   -3,  -20,  -22,
     -42,  -20,  -10,   -5,   -2,  -20,  -23,  -44,
     -29,  -51,  -23,  -15,  -22,  -18,  -50,  -64,
};
static const int pesto_eg_bishop_table[64] = {
     -14,  -21,  -11,   -8,   -7,   -9,  -17,  -24,
      -8,   -4,    7,  -12,   -3,  -13,   -4,  -14,
       2,   -8,    0,   -1,   -2,    6,    0,    4,
      -3,    9,   12,    9,   14,   10,    3,    2,
      -6,    3,   13,   19,    7,   10,   -3,   -9,
     -12,   -3,    8,   10,   13,    3,   -7,  -15,
     -14,  -18,   -7,   -1,    4,   -9,  -15,  -27,
     -23,   -9,  -23,   -5,   -9,  -16,   -5,  -17,
};
static const int pesto_eg_rook_table[64] = {
      13,   10,   18,   15,   12,   12,    8,    5,
      11,   13,   13,   11,   -3,    3,    8,    3,
       7,    7,    7,    5,    4,   -3,   -5,   -3,
       4,    3,   13,    1,    2,    1,   -1,    2,
       3,    5,    8,    4,   -5,   -6,   -8,  -11,
      -4,    0,   -5,   -1,   -7,  -12,   -8,  -16,
      -6,   -6,    0,    2,   -9,   -9,  -11,   -3,
      -9,    2,    3,   -1,   -5,  -13,    4,  -20,
};
static const int pesto_eg_queen_table[64] = {
      -9,   22,   22,   27,   27,   19,   10,   20,
     -17,   20,   32,   41,   58,   25,   30,    0,
     -20,    6,    9,   49,   47,   35,   19,    9,
       3,   22,   24,   45,   57,   40,   57,   36,
     -18,   28,   19,   47,   31,   34,   39,   23,
     -16,  -27,   15,    6,    9,   17,   10,    5,
     -22,  -23,  -30,  -16,  -16,  -23,  -36,  -32,
     -33,  -28,  -22,  -43,   -5,  -32,  -20,  -41,
};
static const int pesto_eg_king_table[64] = {
     -74,  -35,  -18,  -18,  -11,   15,    4,  -17,
     -12,   17,   14,   17,   17,   38,   23,   11,
      10,   17,   23,   15,   20,   45,   44,   13,
      -8,   22,   24,   27,   26,   33,   26,    3,
     -18,   -4,   21,   24,   27,   23,    9,  -11,
     -19,   -3,   11,   21,   23,   16,    7,   -9,
     -27,  -11,    4,   13,   14,    4,   -5,  -17,
     -53,  -34,  -21,  -11,  -28,  -14,  -24,  -43,
};

static const int *pesto_mg[6] = { pesto_mg_pawn_table, pesto_mg_knight_table, pesto_mg_bishop_table,
                                  pesto_mg_rook_table, pesto_mg_queen_table, pesto_mg_king_table };
static const int *pesto_eg[6] = { pesto_eg_pawn_table, pesto_eg_knight_table, pesto_eg_bishop_table,
                                  pesto_eg_rook_table, pesto_eg_queen_table, pesto_eg_king_table };

/* The terms that measured as gains: PeSTO and king safety. The others,
 * with these hand-set weights, did not (docs/overview.md, round 10). */
static EvalOptions eopt = { 1, 0, 0, 1, 0 };

EvalOptions eval_default_options(void) { EvalOptions o = { 1, 0, 0, 1, 0 }; return o; }
void eval_set_options(const EvalOptions *o) { eopt = *o; }

static const U64 FILE_A = 0x0101010101010101ULL;

/* Rank from the side's own point of view, 0..7. */
static int rel_rank(int sq, int side) { return side == WHITE ? sq / 8 : 7 - sq / 8; }

/* Files with at least one pawn of this bitboard, as an 8-bit mask. */
static int pawn_files(U64 pawns)
{
    int f = 0;
    for (int i = 0; i < 8; i++) if (pawns & (FILE_A << i)) f |= 1 << i;
    return f;
}

/* No enemy pawn ahead on its own file or either neighbour. */
static int is_passed(int sq, int side, U64 enemy_pawns)
{
    int file = sq % 8, rank = sq / 8;
    for (int f = file - 1; f <= file + 1; f++) {
        if (f < 0 || f > 7) continue;
        for (int r = side == WHITE ? rank + 1 : rank - 1; r >= 0 && r <= 7; r += side == WHITE ? 1 : -1)
            if (enemy_pawns & (1ULL << (r * 8 + f))) return 0;
    }
    return 1;
}

static const EvalParams hand_set = { {
    -10, -20,                        /* doubled */
    -15, -10,                        /* isolated */
    5, 10, 15, 25, 40, 60,           /* passed, middlegame, ranks 1..6 */
    10, 20, 35, 55, 85, 120,         /* passed, endgame */
    4, 7, 7, 14,                     /* mobility: squares expected, N B R Q */
    4, 5, 2, 1,                      /* mobility weight, middlegame */
    4, 5, 4, 2,                      /* mobility weight, endgame */
    10, -15,                         /* king shield pawn, open file by the king */
    30, 50,                          /* bishop pair */
    20, 10, 10, 5,                   /* rook on an open, a half-open file */
} };
_Static_assert(sizeof(hand_set.v) / sizeof(int) == EP_COUNT, "hand_set lists every parameter");

#include "tuned_params.h"
_Static_assert(TUNED_PARAM_COUNT == EP_COUNT, "tuned_params.h matches the EP_* layout");

static EvalParams params = hand_set;

const EvalParams *eval_default_params(void) { return &hand_set; }
const EvalParams *eval_tuned_params(void)   { return &tuned; }
const EvalParams *eval_params(void)         { return &params; }
void eval_set_params(const EvalParams *p)   { params = *p; }

#define W(i) (params.v[i])

static void pawn_terms(const Position *pos, int side, int *mg, int *eg)
{
    U64 own = pos->bitboards[side == WHITE ? P : p], enemy = pos->bitboards[side == WHITE ? p : P];
    int files = pawn_files(own);
    for (int f = 0; f < 8; f++) {
        int n = count_bits(own & (FILE_A << f));
        if (n > 1) { *mg += W(EP_DOUBLED_MG) * (n - 1); *eg += W(EP_DOUBLED_EG) * (n - 1); }
        int neighbours = (f > 0 && (files >> (f - 1) & 1)) || (f < 7 && (files >> (f + 1) & 1));
        if (n && !neighbours) { *mg += W(EP_ISOLATED_MG) * n; *eg += W(EP_ISOLATED_EG) * n; }
    }
    U64 bb = own;
    while (bb) {
        int sq = pop_lsb(&bb);
        if (is_passed(sq, side, enemy)) {
            int r = rel_rank(sq, side);      /* 1..6 for a pawn */
            *mg += W(EP_PASSED_MG + r - 1);
            *eg += W(EP_PASSED_EG + r - 1);
        }
    }
}

/* Squares each piece reaches beyond what it usually does, weighted. */
static void mobility_terms(const Position *pos, int side, int *mg, int *eg)
{
    int o = side == WHITE ? 0 : 6;
    U64 own = pos->occupancies[side], occ = pos->occupancies[BOTH];
    for (int k = 0; k < 4; k++) {
        U64 bb = pos->bitboards[N + o + k];
        while (bb) {
            int sq = pop_lsb(&bb);
            U64 a = k == 0 ? knight_attacks[sq] : k == 1 ? bishop_attacks(sq, occ)
                  : k == 2 ? rook_attacks(sq, occ) : queen_attacks(sq, occ);
            int n = count_bits(a & ~own) - W(EP_MOB_TYPICAL + k);
            *mg += n * W(EP_MOB_MG + k);
            *eg += n * W(EP_MOB_EG + k);
        }
    }
}

/* The pawns in front of the king, and the files beside it left bare. */
static void king_terms(const Position *pos, int side, int *mg)
{
    U64 king = pos->bitboards[side == WHITE ? K : k];
    if (!king) return;
    int sq = __builtin_ctzll(king), file = sq % 8, rank = sq / 8;
    U64 own = pos->bitboards[side == WHITE ? P : p];
    for (int f = file - 1; f <= file + 1; f++) {
        if (f < 0 || f > 7) continue;
        if (!(own & (FILE_A << f))) *mg += W(EP_OPEN_FILE);
        for (int d = 1; d <= 2; d++) {
            int r = side == WHITE ? rank + d : rank - d;
            if (r >= 0 && r <= 7 && (own & (1ULL << (r * 8 + f)))) { *mg += W(EP_SHIELD); break; }
        }
    }
}

static void extra_terms(const Position *pos, int side, int *mg, int *eg)
{
    int o = side == WHITE ? 0 : 6;
    if (count_bits(pos->bitboards[B + o]) >= 2) { *mg += W(EP_PAIR_MG); *eg += W(EP_PAIR_EG); }
    U64 own = pos->bitboards[P + o], all = pos->bitboards[P] | pos->bitboards[p];
    U64 rooks = pos->bitboards[R + o];
    while (rooks) {
        int f = pop_lsb(&rooks) % 8;
        if (!(all & (FILE_A << f)))      { *mg += W(EP_ROOK_OPEN_MG); *eg += W(EP_ROOK_OPEN_EG); }
        else if (!(own & (FILE_A << f))) { *mg += W(EP_ROOK_HALF_MG); *eg += W(EP_ROOK_HALF_EG); }
    }
}

int evaluate(const Position *pos) {
    int phase = game_phase(pos);
    int mg = 0, eg = 0;
    for (int piece = 0; piece < 12; piece++) {
        U64 bb = pos->bitboards[piece];
        int is_white = piece < 6;
        int type = piece % 6;
        int sign = is_white ? 1 : -1;
        while (bb) {
            int sq = pop_lsb(&bb);
            /* The tables are rank 8 first: WHITE needs mirror() to reach
             * its row; Black's native square already lines up. */
            int table_sq = is_white ? mirror(sq) : sq;
            if (eopt.pesto) {
                mg += sign * (pesto_mg_value[type] + pesto_mg[type][table_sq]);
                eg += sign * (pesto_eg_value[type] + pesto_eg[type][table_sq]);
                continue;
            }
            int pst_val;
            if (type == 5) {
                int kmg = king_pst[table_sq];
                int keg = king_endgame_pst[table_sq];
                pst_val = (kmg * phase + keg * (MAX_PHASE - phase)) / MAX_PHASE;
            } else {
                pst_val = pst[type][table_sq];
            }
            /* The same value in both halves: blending leaves it untouched. */
            mg += sign * (piece_value[piece] + pst_val);
            eg += sign * (piece_value[piece] + pst_val);
        }
    }
    for (int side = WHITE; side <= BLACK; side++) {
        int smg = 0, seg = 0;
        if (eopt.pawns)    pawn_terms(pos, side, &smg, &seg);
        if (eopt.mobility) mobility_terms(pos, side, &smg, &seg);
        if (eopt.king)     king_terms(pos, side, &smg);
        if (eopt.extras)   extra_terms(pos, side, &smg, &seg);
        mg += side == WHITE ? smg : -smg;
        eg += side == WHITE ? seg : -seg;
    }
    int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;
    return (pos->side == WHITE) ? score : -score;
}
