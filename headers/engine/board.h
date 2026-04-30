#ifndef BOARD_H
#define BOARD_H

#include "utils/constants.h"
#include "utils/types.h"

enum {
    CASTLE_WHITE_KING  = 1,
    CASTLE_WHITE_QUEEN = 2,
    CASTLE_BLACK_KING  = 4,
    CASTLE_BLACK_QUEEN = 8
};

typedef struct {
    U64 bitboards[12];
    U64 occupancies[3];

    int side;
    int enpassant;
    int castling;

} Position;

/* functions */
void clear_position(Position* pos);
void update_occupancies(Position* pos);
void init_start_position(Position* pos);
void print_board(Position* pos);

#endif
