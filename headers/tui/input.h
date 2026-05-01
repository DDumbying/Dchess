#ifndef INPUT_H
#define INPUT_H

#include <ncurses.h>

/*
 * Unified input handler.
 * Returns KEY_UP/DOWN/LEFT/RIGHT, '\n' (enter), 27 (ESC),
 * -2 (text command ready in buf), or 0 (absorbed).
 */
int read_key(WINDOW *win, char *buf, int maxlen);

/* Legacy — kept so commands.c compiles unchanged */
static inline int read_command(WINDOW *win, char *buf, int maxlen) {
    return read_key(win, buf, maxlen);
}

#endif
