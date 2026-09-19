#ifndef HASH_H
#define HASH_H

#include "board.h"
#include "utils/types.h"

/* Pieces, side to move, castling rights and en-passant square. Used for
 * repetition detection and as the transposition-table key. Collisions
 * are possible but rare enough for this engine. */
U64 hash_position(const Position *pos);

#endif
