#ifndef MAKE_H
#define MAKE_H

#include "board.h"
#include "move.h"

/* Returns 0 if move leaves own king in check (illegal), 1 otherwise */
int make_move(Position *pos, Move move);

/* NOTE: there's no undo_move(). Every call site currently does a full
 * Position memcpy before calling make_move() and restores it after --
 * see search.c/commands.c. A real incremental make/unmake (restoring
 * just what changed instead of copying the whole struct) would be a
 * meaningful search-speed win, but touches castling rights, captured
 * pieces, and en passant state carefully enough that it deserves its
 * own dedicated pass rather than a quick bolt-on here. */

#endif
