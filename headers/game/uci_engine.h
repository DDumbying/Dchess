#ifndef UCI_ENGINE_H
#define UCI_ENGINE_H

#include <stdio.h>

/* dchess as a UCI engine: reads commands from `in` and answers on `out`
 * until "quit" or end of input. Returns 0. */
int uci_engine_run(FILE *in, FILE *out);

#endif
