#ifndef HASH_H
#define HASH_H

#include "board.h"
#include "utils/types.h"

/* 64-bit hash of a position (pieces, side to move, castling rights,
 * en-passant square). Used for repetition detection and as the
 * transposition-table key. Two calls on equal positions always
 * produce the same value; collisions across different positions are
 * possible but rare enough to be fine for this engine's purposes. */
U64 hash_position(const Position *pos);

#endif
