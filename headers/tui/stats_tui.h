#ifndef STATS_TUI_H
#define STATS_TUI_H

#include "tui/tui.h"
#include <ncurses.h>

/* Full-screen stats for the profiles in `s`, starting at the active one.
 * Returns on Esc; the caller repaints. */
void stats_screen(TUIState *s);

/* Small in-game summary of the active profile over `parent`; any key closes. */
void stats_mini(WINDOW *parent, const TUIState *s);

/* `dchess --stats`: sets ncurses up, shows the page, tears it down.
 * `profile` picks the starting profile; empty means the active one. */
void stats_standalone(const char *profile);

#endif
