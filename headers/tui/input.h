#ifndef INPUT_H
#define INPUT_H

#include <ncurses.h>

/*
 * Unified input handler.
 *
 * insert_mode: pointer to the vim-mode flag stored in TUIState.
 *   0 = normal mode: hjkl/arrows navigate, 'i' enters insert mode.
 *   1 = insert mode: all printable chars go to the command buffer.
 *   ESC always returns to normal mode (and returns 27 to caller).
 *
 * Returns KEY_UP/DOWN/LEFT/RIGHT, '\n' (enter), 27 (ESC),
 * -2 (text command ready in buf), or 0 (absorbed).
 */
int read_key(WINDOW *win, char *buf, int maxlen, int *insert_mode);

/* Legacy — kept so commands.c compiles unchanged */
static inline int read_command(WINDOW *win, char *buf, int maxlen) {
    int dummy = 1;
    return read_key(win, buf, maxlen, &dummy);
}

#endif
