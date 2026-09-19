#include "tui/piece_art.h"

/* Unicode block elements (U+2580-259F). Every row is EXACTLY `cols`
 * cells of single-width characters -- draw_square() relies on it, and a
 * stray wide glyph would shear the board.
 * Shapes are drawn narrower than their box so neighbouring squares keep
 * air between them, and stay distinguishable in silhouette since colour
 * is the only other cue. */

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

/* Smallest first. */
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
    /* Vertical space is scarce (8 squares in <50 rows), so a tier may use
     * the full height but must keep a blank column each side. */
    for (int i = TIER_COUNT - 1; i >= 0; i--)
        if (TIERS[i].rows <= sq_h && TIERS[i].cols + 2 <= sq_w)
            return &TIERS[i];
    return NULL;
}

const wchar_t *piece_art_row(const PieceArtTier *tier, int piece, int row)
{
    if (!tier || row < 0 || row >= tier->rows) return NULL;
    int type = piece % 6;
    if (type < 0) return NULL;
    return tier->art[type][row];
}
