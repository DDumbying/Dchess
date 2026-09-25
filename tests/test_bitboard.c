/* Magic bitboard tests: every lookup must equal the reference ray scan.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include "utils/bitboard.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Every blocker pattern that can matter, for every square. */
static void test_exhaustive(void)
{
    printf("== every relevant occupancy ==\n");

    long rook_bad = 0, bishop_bad = 0, total = 0;
    for (int sq = 0; sq < 64; sq++) {
        U64 sub = 0;
        do {
            if (rook_attacks(sq, sub) != rook_attacks_ref(sq, sub)) rook_bad++;
            total++;
            sub = (sub - rook_mask[sq]) & rook_mask[sq];
        } while (sub);

        sub = 0;
        do {
            if (bishop_attacks(sq, sub) != bishop_attacks_ref(sq, sub)) bishop_bad++;
            sub = (sub - bishop_mask[sq]) & bishop_mask[sq];
        } while (sub);
    }
    check("rook lookup matches the ray scan for all subsets", rook_bad == 0);
    check("bishop lookup matches the ray scan for all subsets", bishop_bad == 0);
    printf("        (%ld rook patterns checked)\n", total);
}

/* Full-board occupancies, including bits the masks deliberately ignore:
 * those must not change the answer. */
static void test_random_boards(void)
{
    printf("== random full-board occupancies ==\n");

    U64 s = 0x0123456789ABCDEFULL;
    long bad = 0;
    for (int i = 0; i < 200000; i++) {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        U64 occ = s * 0x2545F4914F6CDD1DULL;
        int sq = i % 64;
        if (rook_attacks(sq, occ)   != rook_attacks_ref(sq, occ))   bad++;
        if (bishop_attacks(sq, occ) != bishop_attacks_ref(sq, occ)) bad++;
        if (queen_attacks(sq, occ)  != (rook_attacks_ref(sq, occ) |
                                        bishop_attacks_ref(sq, occ))) bad++;
    }
    check("200k random boards agree for rook, bishop and queen", bad == 0);
}

static void test_edges(void)
{
    printf("== edge cases ==\n");

    check("rook on an empty board a1 sees 14 squares",
          count_bits(rook_attacks(0, 0)) == 14);
    check("bishop on an empty board d4 sees 13 squares",
          count_bits(bishop_attacks(27, 0)) == 13);
    check("a blocker's own square is included in the attack set",
          (rook_attacks(0, 1ULL << 3) & (1ULL << 3)) != 0);
    check("squares beyond a blocker are excluded",
          (rook_attacks(0, 1ULL << 3) & (1ULL << 4)) == 0);
}

int main(void)
{
    init_attacks();

    test_exhaustive();
    test_random_boards();
    test_edges();

    if (failures) {
        printf("\n%d bitboard test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll bitboard tests passed.\n");
    return 0;
}
