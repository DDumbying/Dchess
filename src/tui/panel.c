#include "tui/panel.h"
#include "tui/colors.h"
#include <ncurses.h>

/* Half-tone: every theme's canvas is already near-black, so a solid
 * shadow would read as a hole rather than as shade. */
#define SHADOW_CELL ACS_CKBOARD

WINDOW *panel_shadow(int panel_h, int panel_w, int panel_row, int panel_col)
{
    int row = panel_row + PANEL_SHADOW_DY;
    int col = panel_col + PANEL_SHADOW_DX;

    int screen_h, screen_w;
    getmaxyx(stdscr, screen_h, screen_w);

    /* Clip rather than refuse, so a corner panel still casts what fits. */
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

    /* delwin() alone leaves the painted cells on screen. */
    werase(shadow);
    wrefresh(shadow);
    delwin(shadow);
}
