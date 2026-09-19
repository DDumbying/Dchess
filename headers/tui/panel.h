#ifndef TUI_PANEL_H
#define TUI_PANEL_H

#include <ncurses.h>

/* Drop shadow behind a floating panel. Paint it before the panel, which
 * then covers all but the visible lip. Callers keep their own box() and
 * content code unchanged.
 *
 *     WINDOW *sh  = panel_shadow(h, w, row, col);
 *     WINDOW *win = newwin(h, w, row, col);
 *     ...
 *     delwin(win);
 *     panel_shadow_destroy(sh);
 */

/* Twice the horizontal offset: terminal cells are about twice as tall as
 * they are wide, so equal offsets look lopsided. */
#define PANEL_SHADOW_DY 1
#define PANEL_SHADOW_DX 2

/* Takes the panel's geometry, not the shadow's. NULL if it would not fit,
 * which callers can ignore -- destroy(NULL) is a no-op. */
WINDOW *panel_shadow(int panel_h, int panel_w, int panel_row, int panel_col);

/* Call after deleting the panel it sits behind. */
void panel_shadow_destroy(WINDOW *shadow);

#endif
