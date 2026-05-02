#ifndef RENDER_H
#define RENDER_H

#include "tui/tui.h"
#include <ncurses.h>

void init_colors(void);
void render_all(WINDOW *board_win, WINDOW *info_win, WINDOW *eval_bar_win,
                WINDOW *cmd_win, const TUIState *state);

#endif
