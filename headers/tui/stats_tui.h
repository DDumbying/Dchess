#ifndef STATS_TUI_H
#define STATS_TUI_H

/* ─────────────────────────────────────────────────────────────
 * dchess  –  full-screen ncurses statistics overlay
 *
 * show_stats_overlay() blocks until the user presses any key,
 * then returns so the game can continue.
 *
 * draw_stats_overlay() draws into an existing WINDOW without
 * blocking — used by the Tab-hold overlay in tui_run.
 * ───────────────────────────────────────────────────────────── */

#include "utils/stats.h"
#include <ncurses.h>

/* Full-screen blocking stats view (used by --stats flag TUI) */
void show_stats_overlay(const DchessStats *s);

/* Draw full stats + win-rate history graph into an existing window */
void draw_stats_overlay(WINDOW *win, const DchessStats *s);

/* Compact numeric-only stats for the in-game Tab overlay (no graph) */
void draw_stats_compact(WINDOW *win, const DchessStats *s);

#endif /* STATS_TUI_H */
