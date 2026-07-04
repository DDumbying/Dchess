#ifndef ONBOARD_H
#define ONBOARD_H

#include "tui/tui.h"

/* Interactive "new game" screen: lets the player pick side, difficulty,
 * color theme, and starting position (standard or a pasted FEN) with the
 * keyboard instead of CLI flags, then applies the choice by calling
 * tui_init() again with the result. Must be called after ncurses is
 * initialized (initscr/cbreak/noecho/keypad/init_colors) and before the
 * main game windows are created -- see tui_run().
 *
 * Pressing 's' at any point opens the stats overlay (dismiss with any
 * key) and returns to the menu.
 *
 * Returns 1 if the player chose "Start Game" (state has been updated via
 * tui_init()). Returns 0 if the player pressed ESC to quit entirely --
 * *state is left exactly as tui_init() set it up from CLI flags/defaults,
 * but the caller should not proceed to the game in this case; it should
 * tear down ncurses and exit. */
int tui_onboarding(TUIState *state);

#endif
