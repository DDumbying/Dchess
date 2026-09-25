#include "utils/dash.h"
#include <stdio.h>

#define EVAL_H   6
#define EVAL_MIN 4
#define CLOCK_H  6
#define ENGINE_H 5
#define MOVES_MIN 3

int dash_side_width(int cols)
{
    if (cols < 60)  return 0;
    if (cols < 110) return 26;
    return 34;
}

/* Moves take whatever is left. Under pressure the graph shrinks first,
 * then the engine panel goes, then the clocks. */
DashSide dash_side_layout(int height)
{
    DashSide s = { EVAL_H, CLOCK_H, 0, ENGINE_H };

    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.eval_h = EVAL_MIN;
    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.engine_h = 0;
    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.clock_h = 0;
    if (height - (s.eval_h + s.clock_h + s.engine_h) < MOVES_MIN) s.eval_h = 0;

    s.moves_h = height - (s.eval_h + s.clock_h + s.engine_h);
    if (s.moves_h < 0) s.moves_h = 0;
    return s;
}

int dash_graph_fill(int eval_cp, int rows)
{
    if (rows <= 0) return 0;
    if (eval_cp >  DASH_EVAL_CLAMP) eval_cp =  DASH_EVAL_CLAMP;
    if (eval_cp < -DASH_EVAL_CLAMP) eval_cp = -DASH_EVAL_CLAMP;

    long total = rows * 8L;
    long num = (long)(eval_cp + DASH_EVAL_CLAMP) * total;
    long den = 2L * DASH_EVAL_CLAMP;
    return (int)((num + den / 2) / den);
}

int dash_graph_start(int count, int width)
{
    if (width <= 0 || count <= width) return 0;
    return count - width;
}

int dash_ramp_index(int level, int levels, int steps)
{
    if (levels <= 1 || steps <= 1) return 0;
    if (level < 0)       level = 0;
    if (level >= levels) level = levels - 1;
    return level * (steps - 1) / (levels - 1);
}

int dash_bar_fill(long part, long whole, int width)
{
    if (whole <= 0 || width <= 0 || part <= 0) return 0;
    if (part >= whole) return width;
    return (int)((part * width + whole / 2) / whole);
}

void dash_count(long n, char *buf, size_t n_buf)
{
    if      (n >= 1000000) snprintf(buf, n_buf, "%.1fM", n / 1000000.0);
    else if (n >= 1000)    snprintf(buf, n_buf, "%.1fK", n / 1000.0);
    else                   snprintf(buf, n_buf, "%ld", n);
}
