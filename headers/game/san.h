#ifndef SAN_H
#define SAN_H

#include "engine/board.h"
#include "engine/move.h"

/* Longest legal SAN is 7 characters ("Qa1xb2#"), so this has no slack. */
#define SAN_MAXLEN 8

/* Standard Algebraic Notation for `m` played in `before`.
 *
 * Needs the position the move is played FROM, both to name the piece and
 * to work out whether the move has to be disambiguated; the check and
 * mate suffixes come from a private copy made one move on. `m` must be
 * legal in `before`. Writes at most SAN_MAXLEN bytes including the NUL. */
void san_write(const Position *before, Move m, char *out);

#endif
