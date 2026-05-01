#ifndef MOVE_H
#define MOVE_H

#include "../utils/types.h"
#include "../utils/constants.h"

/*
 Move Layout (32-bit):
   bits 0-5:   from square
   bits 6-11:  to square
   bits 12-15: piece
   bits 16-19: flags
*/

#define MOVE(from, to, piece, flags) \
    ((from) | ((to) << 6) | ((piece) << 12) | ((flags) << 16))

#define FROM(m)    ((m) & 0x3F)
#define TO(m)      (((m) >> 6) & 0x3F)
#define PIECE(m)   (((m) >> 12) & 0xF)
#define FLAGS(m)   (((m) >> 16) & 0xFF)

#define FLAG_NONE        0
#define FLAG_CAPTURE     (1 << 0)
#define FLAG_DOUBLE_PUSH (1 << 1)
#define FLAG_ENPASSANT   (1 << 2)
#define FLAG_CASTLING    (1 << 3)
#define FLAG_PROMO_Q     (1 << 4)
#define FLAG_PROMO_R     (1 << 5)
#define FLAG_PROMO_B     (1 << 6)
#define FLAG_PROMO_N     (1 << 7)
#define FLAG_PROMOTION   (FLAG_PROMO_Q|FLAG_PROMO_R|FLAG_PROMO_B|FLAG_PROMO_N)

/* Convert move to algebraic string like "e2e4" or "e7e8q" */
void move_to_str(Move m, char *buf);

/* Parse "e2e4" or "e7e8q" into from/to squares and promo piece */
int parse_move_str(const char *s, int *from, int *to, int *promo);

#endif
