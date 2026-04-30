#ifndef MOVE_H
#define MOVE_H

#include "../utils/types.h"

/*
Move Layout (32-bit int):

bits:
-----------------------------------------
| flags | piece |  to  |  from |
-----------------------------------------
  4bit    4bit    6bit   6bit

> total used: 20 bits
*/

/* --- Encoding --- */

#define MOVE(from, to, piece, flags) \
    ((from) | ((to) << 6) | ((piece) << 12) | ((flags) << 16))

/* --- Decoding --- */

#define FROM(m)    ((m) & 0x3F)
#define TO(m)      (((m) >> 6) & 0x3F)
#define PIECE(m)   (((m) >> 12) & 0xF)
#define FLAGS(m)   (((m) >> 16) & 0xF)

/* --- Flags --- */

enum {
    FLAG_NONE = 0,
    FLAG_CAPTURE = 1,
    FLAG_PROMOTION = 2,
    FLAG_CASTLING = 4,
    FLAG_ENPASSANT = 8
};

#endif
