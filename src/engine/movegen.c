#include "engine/movegen.h"
#include "engine/board.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

int is_attacked(const Position *pos, int sq, int side) {
    /* pawns */
    if (pawn_attacks[side^1][sq] & pos->bitboards[side == WHITE ? P : p]) return 1;
    /* knights */
    if (knight_attacks[sq] & pos->bitboards[side == WHITE ? N : n]) return 1;
    /* bishops/queens */
    U64 ba = bishop_attacks(sq, pos->occupancies[BOTH]);
    if (ba & (pos->bitboards[side == WHITE ? B : b] | pos->bitboards[side == WHITE ? Q : q])) return 1;
    /* rooks/queens */
    U64 ra = rook_attacks(sq, pos->occupancies[BOTH]);
    if (ra & (pos->bitboards[side == WHITE ? R : r] | pos->bitboards[side == WHITE ? Q : q])) return 1;
    /* king */
    if (king_attacks[sq] & pos->bitboards[side == WHITE ? K : k]) return 1;
    return 0;
}

int is_in_check(const Position *pos, int side) {
    U64 king_bb = pos->bitboards[side == WHITE ? K : k];
    if (!king_bb) return 0;
    int king_sq = lsb(king_bb);
    return is_attacked(pos, king_sq, side ^ 1);
}

static void add_move(MoveList *ml, int from, int to, int piece, int flags) {
    ml->moves[ml->count++] = MOVE(from, to, piece, flags);
}

static void add_pawn_move(MoveList *ml, int from, int to, int piece, int flags, int side) {
    int promo_rank = (side == WHITE) ? 7 : 0;
    if (to / 8 == promo_rank) {
        int promos[4] = {FLAG_PROMO_Q, FLAG_PROMO_R, FLAG_PROMO_B, FLAG_PROMO_N};
        for (int i = 0; i < 4; i++)
            add_move(ml, from, to, piece, flags | promos[i]);
    } else {
        add_move(ml, from, to, piece, flags);
    }
}

void generate_moves(const Position *pos, MoveList *ml) {
    ml->count = 0;
    int side = pos->side;
    U64 occ  = pos->occupancies[BOTH];
    U64 own  = pos->occupancies[side];
    U64 opp  = pos->occupancies[side ^ 1];

    if (side == WHITE) {
        /* White pawns */
        U64 pawns = pos->bitboards[P];
        while (pawns) {
            int sq = pop_lsb(&pawns);
            /* single push */
            int fwd = sq + 8;
            if (fwd < 64 && !GET_BIT(occ, fwd)) {
                add_pawn_move(ml, sq, fwd, P, FLAG_NONE, WHITE);
                /* double push */
                if (sq / 8 == 1 && !GET_BIT(occ, sq + 16))
                    add_move(ml, sq, sq + 16, P, FLAG_DOUBLE_PUSH);
            }
            /* captures */
            U64 atk = pawn_attacks[WHITE][sq] & opp;
            while (atk) {
                int to = pop_lsb(&atk);
                add_pawn_move(ml, sq, to, P, FLAG_CAPTURE, WHITE);
            }
            /* en passant */
            if (pos->enpassant != NO_SQ) {
                if (pawn_attacks[WHITE][sq] & (1ULL << pos->enpassant))
                    add_move(ml, sq, pos->enpassant, P, FLAG_ENPASSANT);
            }
        }
        /* castling */
        if ((pos->castling & CASTLE_WHITE_KING) &&
            !GET_BIT(occ, f1) && !GET_BIT(occ, g1) &&
            !is_attacked(pos, e1, BLACK) &&
            !is_attacked(pos, f1, BLACK) &&
            !is_attacked(pos, g1, BLACK))
            add_move(ml, e1, g1, K, FLAG_CASTLING);
        if ((pos->castling & CASTLE_WHITE_QUEEN) &&
            !GET_BIT(occ, d1) && !GET_BIT(occ, c1) && !GET_BIT(occ, b1) &&
            !is_attacked(pos, e1, BLACK) &&
            !is_attacked(pos, d1, BLACK) &&
            !is_attacked(pos, c1, BLACK))
            add_move(ml, e1, c1, K, FLAG_CASTLING);
    } else {
        /* Black pawns */
        U64 pawns = pos->bitboards[p];
        while (pawns) {
            int sq = pop_lsb(&pawns);
            int fwd = sq - 8;
            if (fwd >= 0 && !GET_BIT(occ, fwd)) {
                add_pawn_move(ml, sq, fwd, p, FLAG_NONE, BLACK);
                if (sq / 8 == 6 && !GET_BIT(occ, sq - 16))
                    add_move(ml, sq, sq - 16, p, FLAG_DOUBLE_PUSH);
            }
            U64 atk = pawn_attacks[BLACK][sq] & opp;
            while (atk) {
                int to = pop_lsb(&atk);
                add_pawn_move(ml, sq, to, p, FLAG_CAPTURE, BLACK);
            }
            if (pos->enpassant != NO_SQ) {
                if (pawn_attacks[BLACK][sq] & (1ULL << pos->enpassant))
                    add_move(ml, sq, pos->enpassant, p, FLAG_ENPASSANT);
            }
        }
        /* castling */
        if ((pos->castling & CASTLE_BLACK_KING) &&
            !GET_BIT(occ, f8) && !GET_BIT(occ, g8) &&
            !is_attacked(pos, e8, WHITE) &&
            !is_attacked(pos, f8, WHITE) &&
            !is_attacked(pos, g8, WHITE))
            add_move(ml, e8, g8, k, FLAG_CASTLING);
        if ((pos->castling & CASTLE_BLACK_QUEEN) &&
            !GET_BIT(occ, d8) && !GET_BIT(occ, c8) && !GET_BIT(occ, b8) &&
            !is_attacked(pos, e8, WHITE) &&
            !is_attacked(pos, d8, WHITE) &&
            !is_attacked(pos, c8, WHITE))
            add_move(ml, e8, c8, k, FLAG_CASTLING);
    }

    /* Knights */
    int knight_piece = (side == WHITE) ? N : n;
    U64 knights = pos->bitboards[knight_piece];
    while (knights) {
        int sq = pop_lsb(&knights);
        U64 atk = knight_attacks[sq] & ~own;
        while (atk) {
            int to = pop_lsb(&atk);
            int flags = GET_BIT(opp, to) ? FLAG_CAPTURE : FLAG_NONE;
            add_move(ml, sq, to, knight_piece, flags);
        }
    }

    /* Bishops */
    int bishop_piece = (side == WHITE) ? B : b;
    U64 bishops = pos->bitboards[bishop_piece];
    while (bishops) {
        int sq = pop_lsb(&bishops);
        U64 atk = bishop_attacks(sq, occ) & ~own;
        while (atk) {
            int to = pop_lsb(&atk);
            int flags = GET_BIT(opp, to) ? FLAG_CAPTURE : FLAG_NONE;
            add_move(ml, sq, to, bishop_piece, flags);
        }
    }

    /* Rooks */
    int rook_piece = (side == WHITE) ? R : r;
    U64 rooks = pos->bitboards[rook_piece];
    while (rooks) {
        int sq = pop_lsb(&rooks);
        U64 atk = rook_attacks(sq, occ) & ~own;
        while (atk) {
            int to = pop_lsb(&atk);
            int flags = GET_BIT(opp, to) ? FLAG_CAPTURE : FLAG_NONE;
            add_move(ml, sq, to, rook_piece, flags);
        }
    }

    /* Queens */
    int queen_piece = (side == WHITE) ? Q : q;
    U64 queens = pos->bitboards[queen_piece];
    while (queens) {
        int sq = pop_lsb(&queens);
        U64 atk = queen_attacks(sq, occ) & ~own;
        while (atk) {
            int to = pop_lsb(&atk);
            int flags = GET_BIT(opp, to) ? FLAG_CAPTURE : FLAG_NONE;
            add_move(ml, sq, to, queen_piece, flags);
        }
    }

    /* Kings */
    int king_piece = (side == WHITE) ? K : k;
    U64 kings = pos->bitboards[king_piece];
    while (kings) {
        int sq = pop_lsb(&kings);
        U64 atk = king_attacks[sq] & ~own;
        while (atk) {
            int to = pop_lsb(&atk);
            int flags = GET_BIT(opp, to) ? FLAG_CAPTURE : FLAG_NONE;
            add_move(ml, sq, to, king_piece, flags);
        }
    }
}
