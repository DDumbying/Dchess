#include "engine/move.h"
#include "utils/constants.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

void move_to_str(Move m, char *buf) {
    int from  = FROM(m);
    int to    = TO(m);
    int flags = FLAGS(m);
    buf[0] = 'a' + (from % 8);
    buf[1] = '1' + (from / 8);
    buf[2] = 'a' + (to   % 8);
    buf[3] = '1' + (to   / 8);
    buf[4] = '\0';
    if (flags & FLAG_PROMO_Q) { buf[4] = 'q'; buf[5] = '\0'; }
    else if (flags & FLAG_PROMO_R) { buf[4] = 'r'; buf[5] = '\0'; }
    else if (flags & FLAG_PROMO_B) { buf[4] = 'b'; buf[5] = '\0'; }
    else if (flags & FLAG_PROMO_N) { buf[4] = 'n'; buf[5] = '\0'; }
}

int parse_move_str(const char *s, int *from, int *to, int *promo) {
    if (strlen(s) < 4) return 0;
    if (s[0] < 'a' || s[0] > 'h') return 0;
    if (s[1] < '1' || s[1] > '8') return 0;
    if (s[2] < 'a' || s[2] > 'h') return 0;
    if (s[3] < '1' || s[3] > '8') return 0;
    *from  = (s[0]-'a') + (s[1]-'1')*8;
    *to    = (s[2]-'a') + (s[3]-'1')*8;
    *promo = 0;
    if (s[4]) {
        char c = tolower(s[4]);
        if (c == 'q') *promo = FLAG_PROMO_Q;
        else if (c == 'r') *promo = FLAG_PROMO_R;
        else if (c == 'b') *promo = FLAG_PROMO_B;
        else if (c == 'n') *promo = FLAG_PROMO_N;
    }
    return 1;
}
