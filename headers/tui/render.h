#ifndef RENDER_H
#define RENDER_H

#include "tui/tui.h"
#include "utils/theme.h"
#include <ncurses.h>

/* theme: an index into the built-in theme table (0 = "classic", the
 * original palette). Out-of-range values fall back to 0. Safe to call
 * repeatedly (e.g. for a live preview while a menu is open) — each call
 * just redefines the same custom color slots with the new theme's RGB
 * values and re-applies them via init_pair(). See utils/theme.h for
 * theme_name()/theme_from_name()/theme_count(). */
void init_colors(int theme);

void render_all(WINDOW *board_win, WINDOW *info_win, WINDOW *eval_bar_win,
                WINDOW *cmd_win, const TUIState *state);

#endif
