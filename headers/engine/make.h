#ifndef MAKE_H
#define MAKE_H

#include "board.h"
#include "move.h"

/* Returns 0 if the move leaves its own king in check. */
int make_move(Position *pos, Move move);

/* No undo_move(): every call site copies the whole Position and restores
 * it. An incremental make/unmake would be a real search-speed win, but
 * castling rights, captured pieces and en passant make it its own job. */

#endif
