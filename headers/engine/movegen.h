#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "board.h"
#include "move.h"

typedef struct {
    Move moves[MAX_MOVES];
    int  count;
} MoveList;

void generate_moves(const Position *pos, MoveList *ml);

/* generate_moves() is pseudo-legal, so a non-empty MoveList can still be
 * mate. Pair with is_in_check() to tell mate from stalemate. */
int has_legal_moves(const Position *pos);

/* Is the given side's king in check? */
int is_in_check(const Position *pos, int side);

/* Is a square attacked by the given side? */
int is_attacked(const Position *pos, int sq, int side);

#endif
