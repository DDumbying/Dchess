#ifndef MAKE_H
#define MAKE_H

#include "board.h"
#include "move.h"

/* Returns 0 if move leaves own king in check (illegal), 1 otherwise */
int make_move(Position *pos, Move move);
void undo_move(Position *pos, Move move, const Position *saved);

#endif
