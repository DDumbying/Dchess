#ifndef COMMANDS_H
#define COMMANDS_H

#include "tui/tui.h"

/* Returns 1 if command was handled, 0 if unknown */
int handle_command(TUIState *state, const char *cmd);

#endif
