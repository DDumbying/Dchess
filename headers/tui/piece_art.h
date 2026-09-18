#ifndef TUI_PIECE_ART_H
#define TUI_PIECE_ART_H

#include <wchar.h>

/* ── Multi-row piece art ─────────────────────────────────────────────────
 *
 * The board scales its squares to fill the terminal (see draw_board()),
 * but a piece used to be a single Unicode glyph regardless -- so on a
 * large terminal you got a big square with a tiny character marooned in
 * the middle of it. These tables draw a piece as a silhouette that
 * actually fills the square.
 *
 * Art is a plain silhouette in one colour: the square's background pair
 * still paints the cell, and the art is drawn on top in the piece's
 * foreground pair. That preserves render.c's layering contract (square
 * pair = background only, piece pair = foreground only), so highlights,
 * the cursor, last-move tinting and the check square all keep working
 * without knowing anything about art.
 *
 * Both colours share one silhouette per piece type and are told apart by
 * colour alone. Outline glyphs for White and solid ones for Black is a
 * font-level distinction that does not survive being redrawn as blocks.
 */

/* Tallest tier, so the art can live in a fixed-size table. */
#define PIECE_ART_MAX_ROWS 4

/* A tier is one size of art. Tiers are ordered smallest first; the board
 * picks the largest one that fits the current square. */
typedef struct {
    int rows;
    int cols;
    /* [piece type 0-5: pawn, knight, bishop, rook, queen, king][row] */
    const wchar_t *const (*art)[PIECE_ART_MAX_ROWS];
} PieceArtTier;

/* Number of available tiers. */
int piece_art_tier_count(void);

/* Tier by index (0 = smallest). Never NULL for a valid index. */
const PieceArtTier *piece_art_tier(int index);

/* The largest tier that fits inside a sq_h x sq_w square while leaving
 * at least one blank cell of margin, or NULL if none does -- in which
 * case the caller falls back to the single-glyph rendering. */
const PieceArtTier *piece_art_for_square(int sq_h, int sq_w);

/* One row of art for a piece. `piece` is the engine's 0-11 index; the
 * colour half is ignored, since both colours share a silhouette. */
const wchar_t *piece_art_row(const PieceArtTier *tier, int piece, int row);

#endif
