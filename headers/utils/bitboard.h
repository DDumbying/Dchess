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

/* Slider attacks by magic lookup. Filled by init_attacks(). */
extern U64 rook_mask[64],   rook_magic[64];
extern U64 bishop_mask[64], bishop_magic[64];
extern int rook_shift[64],  bishop_shift[64];
extern U64 rook_table[64][4096];
extern U64 bishop_table[64][512];

static inline U64 rook_attacks(int sq, U64 occ) {
    return rook_table[sq][((occ & rook_mask[sq]) * rook_magic[sq]) >> rook_shift[sq]];
}
static inline U64 bishop_attacks(int sq, U64 occ) {
    return bishop_table[sq][((occ & bishop_mask[sq]) * bishop_magic[sq]) >> bishop_shift[sq]];
}
static inline U64 queen_attacks(int sq, U64 occ) {
    return rook_attacks(sq, occ) | bishop_attacks(sq, occ);
}

/* Reference ray scans: slow, used to build and to test the tables. */
U64 rook_attacks_ref(int sq, U64 occ);
U64 bishop_attacks_ref(int sq, U64 occ);

void init_attacks(void);

#endif
