#ifndef REPLAY_TUI_H
#define REPLAY_TUI_H

#include "tui/tui.h"

/* Both return 1 when the player chose to play on from a position (the
 * state is then set up for tui_run's game), 0 when they left. */
int replay_browse(TUIState *s, ReplayList *l, const char *path);
int replay_open(TUIState *s, const char *path, long offset);

/* Move j of the replay (0 = the first), once the review has both sides
 * of it: its grade, and the centipawns it lost. */
Grade replay_grade(const TUIState *s, int j, int *loss);

#endif
