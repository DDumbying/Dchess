#ifndef TUI_PANELS_H
#define TUI_PANELS_H

#include <ncurses.h>
#include "tui/tui.h"

/* Rounded border with `title` set into the top edge in `accent_pair`. */
void panel_frame(WINDOW *win, const char *title, int accent_pair);

void draw_side_column(WINDOW *side, const TUIState *state);

/* Frame and status line; read_key() draws the input row. */
void draw_command_bar(WINDOW *cmd, const TUIState *state);

#endif
