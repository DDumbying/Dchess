#include "utils/bitboard.h"
#include "utils/constants.h"
#include <string.h>

U64 pawn_attacks[2][64];
U64 knight_attacks[64];
U64 king_attacks[64];

static U64 bishop_masks[64];
static U64 rook_masks[64];

/* ---- leaper attack tables ---- */

static U64 mask_pawn_attacks(int side, int sq) {
    U64 bb = 1ULL << sq, attacks = 0;
    if (side == 0) { /* white */
        if (bb & ~0x0101010101010101ULL) attacks |= (bb << 7);
        if (bb & ~0x8080808080808080ULL) attacks |= (bb << 9);
    } else {
        if (bb & ~0x0101010101010101ULL) attacks |= (bb >> 9);
        if (bb & ~0x8080808080808080ULL) attacks |= (bb >> 7);
    }
    return attacks;
}

static U64 mask_knight_attacks(int sq) {
    U64 bb = 1ULL << sq, a = 0;
    if (bb & ~0x0101010101010101ULL) { a |= bb << 15; a |= bb >> 17; }
    if (bb & ~0x8080808080808080ULL) { a |= bb << 17; a |= bb >> 15; }
    if (bb & ~0x0303030303030303ULL) a |= bb >> 10;
    if (bb & ~0xC0C0C0C0C0C0C0C0ULL) a |= bb << 10;
    if (bb & ~0x0303030303030303ULL) a |= bb << 6;
    if (bb & ~0xC0C0C0C0C0C0C0C0ULL) a |= bb >> 6;
    /* simpler: */
    a = 0;
    U64 b = 1ULL << sq;
    U64 l1 = (b >> 1) & 0x7f7f7f7f7f7f7f7fULL;
    U64 l2 = (b >> 2) & 0x3f3f3f3f3f3f3f3fULL;
    U64 r1 = (b << 1) & 0xfefefefefefefefeULL;
    U64 r2 = (b << 2) & 0xfcfcfcfcfcfcfcfcULL;
    U64 h1 = l1 | r1;
    U64 h2 = l2 | r2;
    a = (h1 << 16) | (h1 >> 16) | (h2 << 8) | (h2 >> 8);
    return a;
}

static U64 mask_king_attacks(int sq) {
    U64 bb = 1ULL << sq, a = 0;
    if (bb >> 8)       a |= bb >> 8;
    if (bb << 8)       a |= bb << 8;
    if ((bb & ~0x0101010101010101ULL) >> 1)  a |= bb >> 1;
    if ((bb & ~0x8080808080808080ULL) << 1)  a |= bb << 1;
    if ((bb & ~0x0101010101010101ULL) >> 9)  a |= bb >> 9;
    if ((bb & ~0x8080808080808080ULL) >> 7)  a |= bb >> 7;
    if ((bb & ~0x0101010101010101ULL) << 7)  a |= bb << 7;
    if ((bb & ~0x8080808080808080ULL) << 9)  a |= bb << 9;
    return a;
}

/* ---- slider attacks (classical approach) ---- */

static U64 ray(int sq, int dr, int df) {
    U64 result = 0;
    int r = sq / 8 + dr, f = sq % 8 + df;
    while (r >= 0 && r < 8 && f >= 0 && f < 8) {
        result |= 1ULL << (r * 8 + f);
        r += dr; f += df;
    }
    return result;
}

static U64 bishop_attacks_slow(int sq, U64 occ) {
    U64 a = 0;
    int dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    for (int d = 0; d < 4; d++) {
        int r = sq/8+dirs[d][0], f = sq%8+dirs[d][1];
        while (r>=0&&r<8&&f>=0&&f<8) {
            U64 bit = 1ULL<<(r*8+f);
            a |= bit;
            if (bit & occ) break;
            r+=dirs[d][0]; f+=dirs[d][1];
        }
    }
    return a;
}

static U64 rook_attacks_slow(int sq, U64 occ) {
    U64 a = 0;
    int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (int d = 0; d < 4; d++) {
        int r = sq/8+dirs[d][0], f = sq%8+dirs[d][1];
        while (r>=0&&r<8&&f>=0&&f<8) {
            U64 bit = 1ULL<<(r*8+f);
            a |= bit;
            if (bit & occ) break;
            r+=dirs[d][0]; f+=dirs[d][1];
        }
    }
    return a;
}

U64 bishop_attacks(int sq, U64 occ) { return bishop_attacks_slow(sq, occ); }
U64 rook_attacks(int sq, U64 occ)   { return rook_attacks_slow(sq, occ); }
U64 queen_attacks(int sq, U64 occ)  { return bishop_attacks(sq,occ)|rook_attacks(sq,occ); }

void init_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        pawn_attacks[0][sq] = mask_pawn_attacks(0, sq);
        pawn_attacks[1][sq] = mask_pawn_attacks(1, sq);
        knight_attacks[sq]  = mask_knight_attacks(sq);
        king_attacks[sq]    = mask_king_attacks(sq);
    }
}
