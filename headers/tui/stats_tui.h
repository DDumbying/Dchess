#ifndef STATS_TUI_H
#define STATS_TUI_H

#include "tui/tui.h"
#include <ncurses.h>

/* Full-screen stats for the profiles in `s`, starting at the active one.
 * With can_replay, ⏎ on a recent game replays it. Returns 1 when the
 * player chose to play on from a replay (the state is set up for the
 * game), 0 on Esc; the caller repaints. */
int  stats_screen(TUIState *s, int can_replay);

/* Small in-game summary of the active profile over `parent`; any key closes. */
void stats_mini(WINDOW *parent, const TUIState *s);

#endif
