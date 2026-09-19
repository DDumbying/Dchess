#include "game/san.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include <stdio.h>
#include <string.h>

static const char PIECE_LETTER[6] = { 'P', 'N', 'B', 'R', 'Q', 'K' };

static int piece_on(const Position *pos, int sq)
{
    for (int i = 0; i < 12; i++)
        if (GET_BIT(pos->bitboards[i], sq)) return i;
    return -1;
}

/* Which of file / rank / both is needed to say which piece moved.
 * Returns 0 for neither, 1 for file, 2 for rank, 3 for both. */
static int disambiguation(const Position *pos, Move m, int type)
{
    int from = FROM(m), to = TO(m);
    int same_file = 0, same_rank = 0, rivals = 0;

    MoveList ml;
    generate_moves(pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Move c = ml.moves[i];
        if (TO(c) != to || FROM(c) == from) continue;

        int p = piece_on(pos, FROM(c));
        if (p < 0 || p % 6 != type) continue;
        if ((p < 6) != (pos->side == WHITE)) continue;

        /* Only moves that could actually be played count as rivals: a
         * pinned piece is not an ambiguity. */
        Position test = *pos;
        if (!make_move(&test, c)) continue;

        rivals++;
        if (FROM(c) % 8 == from % 8) same_file++;
        if (FROM(c) / 8 == from / 8) same_rank++;
    }

    if (!rivals)    return 0;
    if (!same_file) return 1;
    if (!same_rank) return 2;
    return 3;
}

void san_write(const Position *before, Move m, char *out)
{
    int from = FROM(m), to = TO(m), flags = FLAGS(m);
    int piece = piece_on(before, from);
    int type  = (piece >= 0) ? piece % 6 : 0;
    int n = 0;

    if (flags & FLAG_CASTLING) {
        /* King side is the one that ends nearer the h-file. */
        n += snprintf(out + n, SAN_MAXLEN - n, "%s",
                      (to % 8) > (from % 8) ? "O-O" : "O-O-O");
    } else if (type == 0) {
        if (flags & (FLAG_CAPTURE | FLAG_ENPASSANT))
            n += snprintf(out + n, SAN_MAXLEN - n, "%cx", 'a' + (from % 8));
        n += snprintf(out + n, SAN_MAXLEN - n, "%c%d",
                      'a' + (to % 8), (to / 8) + 1);
        if (flags & FLAG_PROMOTION) {
            char promo = (flags & FLAG_PROMO_N) ? 'N' :
                         (flags & FLAG_PROMO_B) ? 'B' :
                         (flags & FLAG_PROMO_R) ? 'R' : 'Q';
            n += snprintf(out + n, SAN_MAXLEN - n, "=%c", promo);
        }
    } else {
        n += snprintf(out + n, SAN_MAXLEN - n, "%c", PIECE_LETTER[type]);

        int dis = disambiguation(before, m, type);
        if (dis & 1) n += snprintf(out + n, SAN_MAXLEN - n, "%c", 'a' + (from % 8));
        if (dis & 2) n += snprintf(out + n, SAN_MAXLEN - n, "%d", (from / 8) + 1);

        if (flags & FLAG_CAPTURE)
            n += snprintf(out + n, SAN_MAXLEN - n, "x");
        n += snprintf(out + n, SAN_MAXLEN - n, "%c%d",
                      'a' + (to % 8), (to / 8) + 1);
    }

    /* Check and mate are properties of the resulting position. */
    Position after = *before;
    if (make_move(&after, m) && is_in_check(&after, after.side)) {
        const char *suffix = has_legal_moves(&after) ? "+" : "#";
        if (n < SAN_MAXLEN - 1) snprintf(out + n, SAN_MAXLEN - n, "%s", suffix);
    }
}
