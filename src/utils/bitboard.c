#include "utils/bitboard.h"
#include "utils/constants.h"
#include <string.h>

U64 pawn_attacks[2][64];
U64 knight_attacks[64];
U64 king_attacks[64];

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
    U64 b  = 1ULL << sq;
    U64 l1 = (b >> 1) & 0x7f7f7f7f7f7f7f7fULL;
    U64 l2 = (b >> 2) & 0x3f3f3f3f3f3f3f3fULL;
    U64 r1 = (b << 1) & 0xfefefefefefefefeULL;
    U64 r2 = (b << 2) & 0xfcfcfcfcfcfcfcfcULL;
    U64 h1 = l1 | r1;
    U64 h2 = l2 | r2;
    return (h1 << 16) | (h1 >> 16) | (h2 << 8) | (h2 >> 8);
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

/* ---- slider attacks: reference ray scan ----
 * Slow, but obviously correct. Used to build the magic tables and as the
 * oracle that tests them. */

U64 bishop_attacks_ref(int sq, U64 occ) {
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

U64 rook_attacks_ref(int sq, U64 occ) {
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

/* ---- slider attacks: magic bitboards ----
 * Only the squares a slider's rays cross can block it, and the last
 * square on each ray never matters (nothing beyond it to block). Masking
 * occupancy down to those "relevant" squares, multiplying by a magic
 * number and shifting maps every blocker pattern to a table slot holding
 * the finished attack set -- one multiply and one load per lookup. */

U64 rook_mask[64],   rook_magic[64];
U64 bishop_mask[64], bishop_magic[64];
int rook_shift[64],  bishop_shift[64];
U64 rook_table[64][4096];
U64 bishop_table[64][512];

static U64 relevant_mask(int sq, int bishop)
{
    static const int RD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    static const int BD[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    const int (*dir)[2] = bishop ? BD : RD;
    U64 m = 0;
    for (int d = 0; d < 4; d++) {
        int r = sq / 8 + dir[d][0], f = sq % 8 + dir[d][1];
        /* stop one short of the edge along this ray */
        while (r + dir[d][0] >= 0 && r + dir[d][0] < 8 &&
               f + dir[d][1] >= 0 && f + dir[d][1] < 8 &&
               r >= 0 && r < 8 && f >= 0 && f < 8) {
            m |= 1ULL << (r * 8 + f);
            r += dir[d][0]; f += dir[d][1];
        }
    }
    return m;
}

/* Found by a seeded search over sparse random candidates, then embedded:
 * searching at start-up took ~190 ms on every launch. Any change here is
 * checked exhaustively against the ray scan by tests/test_bitboard.c. */
static const U64 ROOK_MAGIC[64] = {
    0x1080004008801020ULL, 0x0840092002c03000ULL, 0x1900200010400900ULL,
    0x0880100008000480ULL, 0x4200100420080200ULL, 0x8100020100080400ULL,
    0x0200040110886200ULL, 0x0200008040220411ULL, 0x0404800084400220ULL,
    0x0000401000402000ULL, 0x0086001081220440ULL, 0x0408800800100280ULL,
    0x000a001201040820ULL, 0x8848800200840080ULL, 0x4001000100040200ULL,
    0x0442000102105084ULL, 0x9080010020804100ULL, 0x0040404000201009ULL,
    0x0000808010002009ULL, 0x2200090021d00100ULL, 0x0008008008040080ULL,
    0x0004004002010040ULL, 0x0011040008015042ULL, 0x00000a0001768104ULL,
    0x0000800080204009ULL, 0x2010004140002001ULL, 0x9800200280100080ULL,
    0x1000100080080080ULL, 0x0050500500080100ULL, 0x0000020080040080ULL,
    0x0c10010400420810ULL, 0x1040008200005104ULL, 0x01808240088004a0ULL,
    0x0882804004802000ULL, 0x0880402001001100ULL, 0x2000210409001000ULL,
    0x2000480131001500ULL, 0x0000800400800200ULL, 0x000002380c001003ULL,
    0x4600084882000431ULL, 0x0080002000504000ULL, 0x0300500020004002ULL,
    0x0040408200220011ULL, 0x0010040008004040ULL, 0x0000080004008080ULL,
    0x0010040002008080ULL, 0x2012004881020004ULL, 0x8300842444820011ULL,
    0x0088403882010200ULL, 0x0820400080210100ULL, 0x0110910040a00300ULL,
    0x0801100280080480ULL, 0x0242009008200600ULL, 0x1002000489500200ULL,
    0x0040800200010080ULL, 0x0091800041000080ULL, 0x0000209300488001ULL,
    0x04c1002414824001ULL, 0x020020000b001041ULL, 0x7000100004200901ULL,
    0x8002002004100802ULL, 0x30010002084c0007ULL, 0x0888221800813004ULL,
    0x4000002840840112ULL,
};

static const U64 BISHOP_MAGIC[64] = {
    0x20c0090901061081ULL, 0x0024040094030104ULL, 0x8210810200290200ULL,
    0x0011040484620000ULL, 0x0081104002221000ULL, 0x0009012011001350ULL,
    0x0081010802400380ULL, 0x0000420210010408ULL, 0x0008105002280050ULL,
    0x0001028484040044ULL, 0x2a00880810408804ULL, 0x7020022282000100ULL,
    0x0084040420100a50ULL, 0x000401010840e000ULL, 0x2020020210420888ULL,
    0x0008084202012010ULL, 0x2010400810018800ULL, 0x0445122008020840ULL,
    0x0804100808002008ULL, 0x0008002104110100ULL, 0x0061005820080800ULL,
    0x2001000200820100ULL, 0x480c210084010800ULL, 0x3004442500480420ULL,
    0x1010102240048100ULL, 0x00182009084220a3ULL, 0x8803090a10004205ULL,
    0x0208080040202020ULL, 0x000c044084010040ULL, 0x00a1010002004106ULL,
    0x6008210020640202ULL, 0x1600902112860801ULL, 0x00042008c1220200ULL,
    0x010c042002440140ULL, 0x5022080200040820ULL, 0x0402004042940100ULL,
    0x0860108400008020ULL, 0x000c080022021000ULL, 0x0264080652822100ULL,
    0x4005031221010401ULL, 0x0004502410008400ULL, 0x000500b010a20400ULL,
    0x0415094050080800ULL, 0x080000201800a104ULL, 0x4022a80304000110ULL,
    0x4012140802028020ULL, 0x40200104010100a0ULL, 0x12810806008b0c41ULL,
    0x0020441008080000ULL, 0x2002120084045420ULL, 0x0704020062080002ULL,
    0x0000001084040001ULL, 0x0322200891240200ULL, 0xf040200210024800ULL,
    0x0140824832008042ULL, 0x000210020a004602ULL, 0x0083042805141020ULL,
    0x002c12009a011000ULL, 0x0041a00044140400ULL, 0x00004004020a0202ULL,
    0x0000140010020210ULL, 0x2864160811012200ULL, 0x2060080841082a17ULL,
    0xa010041108003100ULL,
};

static void fill_table(int sq, int bishop)
{
    U64 mask  = relevant_mask(sq, bishop);
    int shift = 64 - count_bits(mask);
    U64 magic = bishop ? BISHOP_MAGIC[sq] : ROOK_MAGIC[sq];
    U64 *table = bishop ? bishop_table[sq] : rook_table[sq];

    /* Every subset of the mask, via the carry-rippler trick. */
    U64 sub = 0;
    do {
        table[(sub * magic) >> shift] =
            bishop ? bishop_attacks_ref(sq, sub) : rook_attacks_ref(sq, sub);
        sub = (sub - mask) & mask;
    } while (sub);

    if (bishop) { bishop_mask[sq] = mask; bishop_magic[sq] = magic; bishop_shift[sq] = shift; }
    else        { rook_mask[sq]   = mask; rook_magic[sq]   = magic; rook_shift[sq]   = shift; }
}

void init_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        pawn_attacks[0][sq] = mask_pawn_attacks(0, sq);
        pawn_attacks[1][sq] = mask_pawn_attacks(1, sq);
        knight_attacks[sq]  = mask_knight_attacks(sq);
        king_attacks[sq]    = mask_king_attacks(sq);
    }
    for (int sq = 0; sq < 64; sq++) {
        fill_table(sq, 0);
        fill_table(sq, 1);
    }
}
