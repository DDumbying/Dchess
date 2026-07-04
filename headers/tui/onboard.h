#ifndef ONBOARD_H
#define ONBOARD_H

#include "tui/tui.h"

/* Interactive "new game" screen: lets the player pick side, difficulty,
 * and starting position (standard or a pasted FEN) with the keyboard
 * instead of CLI flags, then applies the choice by calling tui_init()
 * again with the result. Must be called after ncurses is initialized
 * (initscr/cbreak/noecho/keypad/init_colors) and before the main game
 * windows are created -- see tui_run().
 *
 * ESC skips the screen entirely and leaves *state exactly as it was
 * (i.e. whatever tui_init() set up from CLI flags/defaults). */
void tui_onboarding(TUIState *state);

#endif
