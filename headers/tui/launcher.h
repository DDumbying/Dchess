#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "tui/tui.h"

/* Full-screen launcher: profiles, recent games and the new-game setup.
 * Call after ncurses is up and before the game windows exist; applies the
 * choice through tui_init(). Returns 1 to start, 0 when the player quits. */
int tui_launcher(TUIState *state);

#endif
