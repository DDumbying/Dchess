#ifndef DASH_H
#define DASH_H

#include <stddef.h>

/* Layout and graph maths for the dashboard. No ncurses, so it can be
 * tested; tui/panels.c does the drawing. */

#define DASH_EVAL_CLAMP 500   /* centipawns that fill or empty the graph */

typedef struct { int eval_h, clock_h, moves_h, engine_h; } DashSide;

int      dash_side_width(int cols);
DashSide dash_side_layout(int height);

/* Eighths of a column to fill: -CLAMP is empty, 0 half, +CLAMP full. */
int dash_graph_fill(int eval_cp, int rows);

/* First history index shown when `count` values must fit `width` columns. */
int dash_graph_start(int count, int width);

int  dash_ramp_index(int level, int levels, int steps);
int  dash_bar_fill(long part, long whole, int width);
void dash_count(long n, char *buf, size_t n_buf);

#endif
