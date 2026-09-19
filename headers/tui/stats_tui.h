#ifndef STATS_TUI_H
#define STATS_TUI_H

#include "utils/stats.h"
#include <ncurses.h>

/* Blocks until a key is pressed. Used by --stats. */
void show_stats_overlay(const DchessStats *s);

/* Full stats plus the win-rate graph, into an existing window. */
void draw_stats_overlay(WINDOW *win, const DchessStats *s);

/* Small centred popup for in-game quick stats (Tab). */
void draw_stats_mini(WINDOW *parent, const DchessStats *s);

#endif
