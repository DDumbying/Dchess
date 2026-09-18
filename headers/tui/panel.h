#ifndef TUI_PANEL_H
#define TUI_PANEL_H

#include <ncurses.h>

/* ── Panel drop shadows ──────────────────────────────────────────────────
 *
 * Every floating panel in the UI -- the onboarding screen, the game-over
 * popup, the Tab statistics popup -- casts the same shadow, so the logic
 * lives here once rather than in each of them.
 *
 * The shadow is a separate window placed behind the panel and offset
 * down-and-right. It is painted first; refreshing the panel on top then
 * covers everything but the visible lip along the bottom and right.
 * Doing it this way means a caller keeps its existing box()/content code
 * completely unchanged -- it only has to create the shadow before its
 * own window and destroy it after.
 *
 * Usage:
 *     WINDOW *sh  = panel_shadow(h, w, row, col);
 *     WINDOW *win = newwin(h, w, row, col);
 *     ... draw and wrefresh(win) as usual ...
 *     delwin(win);
 *     panel_shadow_destroy(sh);
 */

/* Horizontal offset is twice the vertical one because terminal cells are
 * roughly twice as tall as they are wide -- equal offsets would look
 * lopsided rather than like light from one corner. */
#define PANEL_SHADOW_DY 1
#define PANEL_SHADOW_DX 2

/* Creates and paints the shadow for a panel of this geometry. Pass the
 * panel's own position and size, not the shadow's -- the offset is
 * applied here. Returns NULL if the shadow would not fit on screen, in
 * which case the panel simply renders without one; callers do not need
 * to check, since panel_shadow_destroy(NULL) is a no-op. */
WINDOW *panel_shadow(int panel_h, int panel_w, int panel_row, int panel_col);

/* Destroys a shadow window. Call AFTER deleting the panel it sits
 * behind, so the panel is not briefly left floating over a stale
 * shadow. Safe to call with NULL. */
void panel_shadow_destroy(WINDOW *shadow);

#endif
