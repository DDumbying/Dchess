#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"

#define SET_BIT(bb, sq)   ((bb) |= (1ULL << (sq)))
#define CLEAR_BIT(bb, sq) ((bb) &= ~(1ULL << (sq)))
#define GET_BIT(bb, sq)   ((bb) & (1ULL << (sq)))

#endif
