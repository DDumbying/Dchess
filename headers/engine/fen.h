#ifndef FEN_H
#define FEN_H

#include <stddef.h>
#include "board.h"

/* Returns 1 on success. On failure returns 0 and leaves *pos completely
 * untouched, so a caller can try a string and fall back without saving a
 * copy first.
 *
 * halfmove_clock and fullmove_number take the last two fields if
 * non-NULL; Position does not track them. Both are optional in the input
 * and default to 0 and 1, which is common for pasted FENs. */
int parse_fen(const char *fen, Position *pos,
              int *halfmove_clock, int *fullmove_number);

/* buf must be at least FEN_BUFSIZE bytes. */
#define FEN_BUFSIZE 96
void position_to_fen(const Position *pos, int halfmove_clock,
                      int fullmove_number, char *buf, size_t bufsize);

#endif
