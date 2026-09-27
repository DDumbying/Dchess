#ifndef REPLAY_TUI_H
#define REPLAY_TUI_H

#include "tui/tui.h"

/* Both return 1 when the player chose to play on from a position (the
 * state is then set up for tui_run's game), 0 when they left. */
int replay_browse(TUIState *s, ReplayList *l, const char *path);
int replay_open(TUIState *s, const char *path, long offset);

#endif
