#include "tui/piece_art.h"

/* ── The art ─────────────────────────────────────────────────────────────
 * Silhouettes drawn from the Unicode block-elements range (U+2580-259F),
 * quadrants included. Every row of a tier is EXACTLY `cols` cells wide
 * and every character is single-width -- draw_square() relies on that to
 * keep columns aligned, and a stray wide glyph would shear the board.
 *
 * The shapes deliberately do NOT fill their tier: they are drawn a cell
 * or two narrower than the box so that neighbouring squares keep some
 * air between them. A piece that fills its square edge to edge makes the
 * board read as a solid slab rather than as 64 squares.
 *
 * Shapes stay distinguishable in silhouette, since colour is the only
 * other cue: the rook is crenellated, the queen has a spiked crown, the
 * king a cross, the bishop a cleft mitre, and the knight is the only
 * asymmetric piece. */

static const wchar_t *const ART_SMALL[6][PIECE_ART_MAX_ROWS] = {
    /* Pawn */ {
        L"  \u2584  ",
        L"  \u2588  ",
        L" \u2584\u2588\u2584 ",
    },
    /* Knight */ {
        L" \u2584\u2588\u2596 ",
        L" \u2590\u2588\u2588 ",
        L" \u2584\u2588\u2584 ",
    },
    /* Bishop */ {
        L"  \u259f  ",
        L" \u2590\u2588\u258c ",
        L" \u2584\u2588\u2584 ",
    },
    /* Rook */ {
        L" \u2588\u2584\u2588 ",
        L" \u2588\u2588\u2588 ",
        L" \u2584\u2588\u2584 ",
    },
    /* Queen */ {
        L" \u2599\u2584\u259f ",
        L" \u2588\u2588\u2588 ",
        L" \u2584\u2588\u2584 ",
    },
    /* King */ {
        L" \u2584\u2588\u2584 ",
        L" \u2588\u2588\u2588 ",
        L" \u2584\u2588\u2584 ",
    },
};

static const wchar_t *const ART_LARGE[6][PIECE_ART_MAX_ROWS] = {
    /* Pawn */ {
        L"  \u2584\u2584\u2584  ",
        L"  \u259d\u2588\u2598  ",
        L"   \u2588   ",
        L" \u2597\u2588\u2588\u2588\u2596 ",
    },
    /* Knight */ {
        L"  \u2584\u2588\u2588\u2596 ",
        L" \u259f\u2588\u2588\u2588\u2588 ",
        L" \u2580\u2598\u2590\u2588\u2588 ",
        L" \u2597\u2588\u2588\u2588\u2596 ",
    },
    /* Bishop */ {
        L"   \u259f\u2596  ",
        L"  \u259f\u2588\u2599  ",
        L"  \u259d\u2588\u2598  ",
        L" \u2597\u2588\u2588\u2588\u2596 ",
    },
    /* Rook */ {
        L" \u2588\u2584\u2588\u2584\u2588 ",
        L" \u2590\u2588\u2588\u2588\u258c ",
        L"  \u2588\u2588\u2588  ",
        L" \u2597\u2588\u2588\u2588\u2596 ",
    },
    /* Queen */ {
        L" \u2599\u2584\u2588\u2584\u259f ",
        L" \u2590\u2588\u2588\u2588\u258c ",
        L"  \u259c\u2588\u259b  ",
        L" \u2597\u2588\u2588\u2588\u2596 ",
    },
    /* King */ {
        L"   \u2588   ",
        L" \u2584\u2584\u2588\u2584\u2584 ",
        L" \u2590\u2588\u2588\u2588\u258c ",
        L" \u2597\u2588\u2588\u2588\u2596 ",
    },
};

/* Smallest first: piece_art_for_square() walks backwards to find the
 * largest tier that fits. */
static const PieceArtTier TIERS[] = {
    { 3, 5, ART_SMALL },
    { 4, 7, ART_LARGE },
};
#define TIER_COUNT ((int)(sizeof(TIERS) / sizeof(TIERS[0])))

int piece_art_tier_count(void) { return TIER_COUNT; }

const PieceArtTier *piece_art_tier(int index)
{
    if (index < 0 || index >= TIER_COUNT) index = 0;
    return &TIERS[index];
}

const PieceArtTier *piece_art_for_square(int sq_h, int sq_w)
{
    /* Vertical space is the scarce one -- the board is 8 squares tall in
     * a terminal that is rarely more than 50 rows -- so a tier may use a
     * square's full height, but must keep a blank column each side so
     * neighbouring pieces never touch horizontally. The rest of the
     * breathing room comes from the art being drawn narrower than its
     * own box (see above). */
    for (int i = TIER_COUNT - 1; i >= 0; i--)
        if (TIERS[i].rows <= sq_h && TIERS[i].cols + 2 <= sq_w)
            return &TIERS[i];
    return NULL;
}

const wchar_t *piece_art_row(const PieceArtTier *tier, int piece, int row)
{
    if (!tier || row < 0 || row >= tier->rows) return NULL;
    int type = piece % 6;   /* both colours share a silhouette */
    if (type < 0) return NULL;
    return tier->art[type][row];
}
