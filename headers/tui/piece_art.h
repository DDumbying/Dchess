#ifndef TUI_PIECE_ART_H
#define TUI_PIECE_ART_H

#include <wchar.h>

/* Multi-row piece silhouettes, so a piece fills its square instead of
 * sitting in it as one glyph. Art is drawn in the piece's foreground pair
 * over the square's background pair, preserving render.c's layering
 * contract. Both colours share a silhouette and differ only by colour. */

#define PIECE_ART_MAX_ROWS 4

/* Tiers are ordered smallest first. */
typedef struct {
    int rows;
    int cols;
    /* [pawn, knight, bishop, rook, queen, king][row] */
    const wchar_t *const (*art)[PIECE_ART_MAX_ROWS];
} PieceArtTier;

int piece_art_tier_count(void);
const PieceArtTier *piece_art_tier(int index);

/* Largest tier fitting the square, or NULL -- caller then falls back to
 * the single-glyph rendering. */
const PieceArtTier *piece_art_for_square(int sq_h, int sq_w);

/* `piece` is the engine's 0-11 index; the colour half is ignored. */
const wchar_t *piece_art_row(const PieceArtTier *tier, int piece, int row);

#endif
