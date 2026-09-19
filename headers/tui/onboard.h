#ifndef ONBOARD_H
#define ONBOARD_H

#include "tui/tui.h"

/* Interactive "new game" screen: side, difficulty, theme and starting
 * position. Call after ncurses is up and before the game windows exist.
 * Applies the choice by calling tui_init() again.
 *
 * Returns 1 to start the game, 0 if the player pressed ESC -- in which
 * case the caller must tear down ncurses and exit rather than proceed. */
int tui_onboarding(TUIState *state);

#endif
