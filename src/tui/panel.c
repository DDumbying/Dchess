#include "tui/panel.h"
#include "tui/colors.h"
#include <ncurses.h>

/* Half-tone block. A solid block would read as a hole punched in the
 * screen rather than as shade, because every theme's canvas is already
 * close to black -- there is nothing for a black shadow to be darker
 * than. Stippling it keeps the shadow legible on a dark backdrop. */
#define SHADOW_CELL ACS_CKBOARD

WINDOW *panel_shadow(int panel_h, int panel_w, int panel_row, int panel_col)
{
    int row = panel_row + PANEL_SHADOW_DY;
    int col = panel_col + PANEL_SHADOW_DX;

    int screen_h, screen_w;
    getmaxyx(stdscr, screen_h, screen_w);

    /* Clip to the screen rather than refusing outright: a panel flush
     * against the bottom-right corner should still cast whatever part of
     * its shadow fits. */
    int h = panel_h, w = panel_w;
    if (row + h > screen_h) h = screen_h - row;
    if (col + w > screen_w) w = screen_w - col;
    if (h <= 0 || w <= 0) return NULL;

    WINDOW *shadow = newwin(h, w, row, col);
    if (!shadow) return NULL;

    wattron(shadow, COLOR_PAIR(CP_SHADOW) | A_DIM);
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            mvwaddch(shadow, r, c, SHADOW_CELL);
    wattroff(shadow, COLOR_PAIR(CP_SHADOW) | A_DIM);

    wrefresh(shadow);
    return shadow;
}

void panel_shadow_destroy(WINDOW *shadow)
{
    if (!shadow) return;

    /* Erase before deleting: delwin() alone leaves the painted cells on
     * screen until something else happens to redraw that region, which
     * for the game-over popup is not guaranteed. */
    werase(shadow);
    wrefresh(shadow);
    delwin(shadow);
}
