#ifndef ENGINES_TUI_H
#define ENGINES_TUI_H

#include "utils/engines.h"

/* Add, edit, test and delete engines. Saves after every change and
 * returns on Esc. */
void engines_screen(EngineList *list);

#endif
