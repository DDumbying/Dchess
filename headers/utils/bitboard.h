#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"

#define SET_BIT(bb, sq)   ((bb) |=  (1ULL << (sq)))
#define CLEAR_BIT(bb, sq) ((bb) &= ~(1ULL << (sq)))
#define GET_BIT(bb, sq)   ((bb) &   (1ULL << (sq)))

static inline int count_bits(U64 bb) {
    return __builtin_popcountll(bb);
}

static inline int lsb(U64 bb) {
    return __builtin_ctzll(bb);
}

static inline int pop_lsb(U64 *bb) {
    int sq = lsb(*bb);
    *bb &= *bb - 1;
    return sq;
}

/* Pre-computed attack tables */
extern U64 pawn_attacks[2][64];
extern U64 knight_attacks[64];
extern U64 king_attacks[64];

/* Slider attacks (hyperbola quintessence) */
U64 bishop_attacks(int sq, U64 occ);
U64 rook_attacks(int sq, U64 occ);
U64 queen_attacks(int sq, U64 occ);

void init_attacks(void);

#endif
