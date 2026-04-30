#include "engine/board.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include <string.h>
#include <stdio.h>

void clear_position(Position* pos) {
    memset(pos, 0, sizeof(Position));

    pos->enpassant = -1;
}

void update_occupancies(Position* pos) {
    pos->occupancies[0] = 0ULL; // white
    pos->occupancies[1] = 0ULL; // black
    pos->occupancies[2] = 0ULL; // both

    // white pieces (0 → 5)
    for (int i = 0; i < 6; i++)
        pos->occupancies[0] |= pos->bitboards[i];

    // black pieces (6 → 11)
    for (int i = 6; i < 12; i++)
        pos->occupancies[1] |= pos->bitboards[i];

    // both
    pos->occupancies[2] =
        pos->occupancies[0] | pos->occupancies[1];
}

void init_start_position(Position* pos) {
    clear_position(pos);

    // pawns
    pos->bitboards[P] = 0x000000000000FF00ULL;
    pos->bitboards[p] = 0x00FF000000000000ULL;

    // rooks
    pos->bitboards[R] = 0x0000000000000081ULL;
    pos->bitboards[r] = 0x8100000000000000ULL;

    // knights
    pos->bitboards[N] = 0x0000000000000042ULL;
    pos->bitboards[n] = 0x4200000000000000ULL;

    // bishops
    pos->bitboards[B] = 0x0000000000000024ULL;
    pos->bitboards[b] = 0x2400000000000000ULL;

    // queens
    pos->bitboards[Q] = 0x0000000000000008ULL;
    pos->bitboards[q] = 0x0800000000000000ULL;

    // kings
    pos->bitboards[K] = 0x0000000000000010ULL;
    pos->bitboards[k] = 0x1000000000000000ULL;

    pos->side = WHITE;
    pos->enpassant = -1;

    pos->castling =
        CASTLE_WHITE_KING |
        CASTLE_WHITE_QUEEN |
        CASTLE_BLACK_KING |
        CASTLE_BLACK_QUEEN;

    update_occupancies(pos);
}

void print_board(Position* pos) {
    for (int rank = 7; rank >= 0; rank--) {
        printf("%d  ", rank + 1);

        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            int piece = -1;

            for (int i = 0; i < 12; i++) {
                if (pos->bitboards[i] & (1ULL << sq)) {
                    piece = i;
                    break;
                }
            }

            char c = '.';

            if (piece != -1) {
                char symbols[] = "PNBRQKpnbrqk";
                c = symbols[piece];
            }

            printf("%c ", c);
        }

        printf("\n");
    }

    printf("\n   a b c d e f g h\n\n");
}
