#ifndef ART_H
#define ART_H

#include "engine/board.h"
#include <ncurses.h>

/* The dchess logo: three rows, or one row of plain text when small. */
void draw_logo(WINDOW *w, int y, int x, int small);
int  logo_width(int small);

/* 8 rows x 16 columns in the theme's board colours, rank 8 on top. */
void draw_mini_board(WINDOW *w, int y, int x, const Position *pos);

#endif
