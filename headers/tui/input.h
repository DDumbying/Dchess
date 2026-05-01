#ifndef INPUT_H
#define INPUT_H

#include "tui/tui.h"
#include <ncurses.h>

/* Read a command string from the command window, returns 1 if entered */
int read_command(WINDOW *cmd_win, char *buf, int maxlen);

#endif
