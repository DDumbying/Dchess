#include "engine/fen.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Copies the next whitespace-delimited token from *p into out (bounded
 * to outsz, always NUL-terminated), advances *p past it, and returns
 * 1 -- or returns 0 if there was no token left (only trailing/leading
 * whitespace remained). Doesn't mutate the caller's string. */
static int next_token(const char **p, char *out, size_t outsz)
{
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '\0') return 0;

    size_t i = 0;
    while (**p != '\0' && **p != ' ' && **p != '\t') {
        if (i + 1 < outsz) out[i++] = **p;
        (*p)++;
    }
    out[i] = '\0';
    return 1;
}

static int piece_from_char(char c, int *piece_out)
{
    switch (c) {
        case 'P': *piece_out = P; return 1;
        case 'N': *piece_out = N; return 1;
        case 'B': *piece_out = B; return 1;
        case 'R': *piece_out = R; return 1;
        case 'Q': *piece_out = Q; return 1;
        case 'K': *piece_out = K; return 1;
        case 'p': *piece_out = p; return 1;
        case 'n': *piece_out = n; return 1;
        case 'b': *piece_out = b; return 1;
        case 'r': *piece_out = r; return 1;
        case 'q': *piece_out = q; return 1;
        case 'k': *piece_out = k; return 1;
        default:  return 0;
    }
}

/* Parses the piece-placement field (ranks 8 down to 1, '/'-separated)
 * into pos. Returns 1 on success, 0 on any malformed rank. */
static int parse_placement(const char *field, Position *pos)
{
    int rank = 7; /* rank 8 first, per FEN */
    const char *r = field;

    while (1) {
        int file = 0;
        while (*r != '/' && *r != '\0') {
            if (isdigit((unsigned char)*r)) {
                int n = *r - '0';
                if (n < 1 || n > 8) return 0;
                file += n;
            } else {
                int piece;
                if (!piece_from_char(*r, &piece)) return 0;
                if (file > 7) return 0;
                SET_BIT(pos->bitboards[piece], rank * 8 + file);
                file++;
            }
            if (file > 8) return 0;
            r++;
        }
        if (file != 8) return 0;

        if (rank == 0) break;   /* just finished rank 1 -- done */
        if (*r != '/') return 0; /* expected another rank to follow */
        r++;
        rank--;
    }
    return *r == '\0';
}

static int parse_square(const char *s, int *sq_out)
{
    if (strlen(s) != 2) return 0;
    char file = s[0], rankc = s[1];
    if (file < 'a' || file > 'h') return 0;
    if (rankc < '1' || rankc > '8') return 0;
    *sq_out = (rankc - '1') * 8 + (file - 'a');
    return 1;
}

int parse_fen(const char *fen, Position *pos,
              int *halfmove_clock, int *fullmove_number)
{
    if (!fen) return 0;

    char placement[128], side_tok[8], castling_tok[8], ep_tok[8];
    char half_tok[16], full_tok[16];

    const char *p = fen;
    if (!next_token(&p, placement, sizeof(placement))) return 0;
    if (!next_token(&p, side_tok,  sizeof(side_tok)))   return 0;
    if (!next_token(&p, castling_tok, sizeof(castling_tok))) return 0;
    if (!next_token(&p, ep_tok, sizeof(ep_tok))) return 0;
    int have_half = next_token(&p, half_tok, sizeof(half_tok));
    int have_full = next_token(&p, full_tok, sizeof(full_tok));

    Position tmp;
    clear_position(&tmp);

    if (!parse_placement(placement, &tmp)) return 0;

    if (strcmp(side_tok, "w") == 0)      tmp.side = WHITE;
    else if (strcmp(side_tok, "b") == 0) tmp.side = BLACK;
    else return 0;

    tmp.castling = 0;
    if (strcmp(castling_tok, "-") != 0) {
        for (const char *c = castling_tok; *c; c++) {
            switch (*c) {
                case 'K': tmp.castling |= CASTLE_WHITE_KING;  break;
                case 'Q': tmp.castling |= CASTLE_WHITE_QUEEN; break;
                case 'k': tmp.castling |= CASTLE_BLACK_KING;  break;
                case 'q': tmp.castling |= CASTLE_BLACK_QUEEN; break;
                default:  return 0;
            }
        }
    }

    if (strcmp(ep_tok, "-") == 0) {
        tmp.enpassant = NO_SQ;
    } else {
        int sq;
        if (!parse_square(ep_tok, &sq)) return 0;
        tmp.enpassant = sq;
    }

    int hm = 0, fm = 1;
    if (have_half) {
        if (sscanf(half_tok, "%d", &hm) != 1 || hm < 0) return 0;
    }
    if (have_full) {
        if (sscanf(full_tok, "%d", &fm) != 1 || fm < 1) return 0;
    }

    update_occupancies(&tmp);

    *pos = tmp;
    if (halfmove_clock)  *halfmove_clock  = hm;
    if (fullmove_number) *fullmove_number = fm;
    return 1;
}

void position_to_fen(const Position *pos, int halfmove_clock,
                      int fullmove_number, char *buf, size_t bufsize)
{
    static const char symbols[] = "PNBRQKpnbrqk";
    char placement[80];
    int idx = 0;

    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            int found = -1;
            for (int pc = 0; pc < 12; pc++) {
                if (GET_BIT(pos->bitboards[pc], sq)) { found = pc; break; }
            }
            if (found < 0) {
                empty++;
            } else {
                if (empty > 0) { placement[idx++] = (char)('0' + empty); empty = 0; }
                placement[idx++] = symbols[found];
            }
        }
        if (empty > 0) placement[idx++] = (char)('0' + empty);
        if (rank > 0) placement[idx++] = '/';
    }
    placement[idx] = '\0';

    char castling[5];
    int ci = 0;
    if (pos->castling & CASTLE_WHITE_KING)  castling[ci++] = 'K';
    if (pos->castling & CASTLE_WHITE_QUEEN) castling[ci++] = 'Q';
    if (pos->castling & CASTLE_BLACK_KING)  castling[ci++] = 'k';
    if (pos->castling & CASTLE_BLACK_QUEEN) castling[ci++] = 'q';
    if (ci == 0) castling[ci++] = '-';
    castling[ci] = '\0';

    snprintf(buf, bufsize, "%s %s %s %s %d %d",
             placement,
             pos->side == WHITE ? "w" : "b",
             castling,
             pos->enpassant == NO_SQ ? "-" : sq_name(pos->enpassant),
             halfmove_clock,
             fullmove_number);
}
