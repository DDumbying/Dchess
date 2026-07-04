#ifndef FEN_H
#define FEN_H

#include <stddef.h>
#include "board.h"

/* Parse a FEN string into *pos.
 *
 * On success, *pos is fully populated (bitboards, side, castling,
 * en passant, occupancies) and 1 is returned. On failure, 0 is
 * returned and *pos is left completely untouched -- callers can try
 * a string, fall back to the previous position, and report an error
 * without needing to save a copy first.
 *
 * halfmove_clock and fullmove_number receive the last two FEN fields
 * if non-NULL (Position itself doesn't track them -- the TUI keeps
 * its own halfmove_clock for the 50-move rule). Both fields are
 * optional in the input string and default to 0 and 1 respectively
 * if the FEN omits them, which is common for FENs pasted from
 * puzzle sites and lichess/chess.com analysis boards. */
int parse_fen(const char *fen, Position *pos,
              int *halfmove_clock, int *fullmove_number);

/* Write a FEN string for *pos into buf. buf must be at least
 * FEN_BUFSIZE bytes; that's comfortably more than any legal position
 * can produce. */
#define FEN_BUFSIZE 96
void position_to_fen(const Position *pos, int halfmove_clock,
                      int fullmove_number, char *buf, size_t bufsize);

#endif
