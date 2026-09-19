#ifndef INPUT_H
#define INPUT_H

#include <ncurses.h>

/* Unified input handler.
 *
 * insert_mode points at the vim-mode flag in TUIState: 0 = normal
 * (hjkl/arrows navigate, 'i' enters insert), 1 = insert (printable chars
 * go to the command buffer). ESC always returns to normal.
 *
 * Returns an arrow key, '\n', 27 (ESC), 'u', KEY_RESIZE, -2 (command
 * ready in buf) or 0 (absorbed). */
int read_key(WINDOW *win, char *buf, int maxlen, int *insert_mode);

#endif
