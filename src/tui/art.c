#include "tui/art.h"
#include "tui/colors.h"
#include "tui/render.h"
#include "utils/bitboard.h"

static const char *LOGO[3] = {
    "╺┳┓┏━╸╻ ╻┏━╸┏━┓┏━┓",
    " ┃┃┃  ┣━┫┣╸ ┗━┓┗━┓",
    "╺┻┛┗━╸╹ ╹┗━╸┗━┛┗━┛",
};

int logo_width(int small) { return small ? 6 : 18; }

void draw_logo(WINDOW *w, int y, int x, int small)
{
    wattron(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
    if (small) mvwaddstr(w, y, x, "dchess");
    else
        for (int i = 0; i < 3; i++) mvwaddstr(w, y + i, x, LOGO[i]);
    wattroff(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
}

void draw_mini_board(WINDOW *w, int y, int x, const Position *pos)
{
    for (int rank = 7; rank >= 0; rank--)
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file, piece = -1;
            for (int i = 0; i < 12 && piece < 0; i++)
                if (GET_BIT(pos->bitboards[i], sq)) piece = i;
            int light = (rank + file) % 2 != 0;
            int r = y + 7 - rank, c = x + file * 2;
            wattron(w, COLOR_PAIR(light ? CP_LIGHT : CP_DARK));
            mvwaddstr(w, r, c, "  ");
            wattroff(w, COLOR_PAIR(light ? CP_LIGHT : CP_DARK));
            if (piece < 0) continue;
            int pair = piece < 6 ? (light ? CP_W_LIGHT : CP_W_DARK)
                                 : (light ? CP_B_LIGHT : CP_B_DARK);
            render_piece(w, r, c, piece, COLOR_PAIR(pair) | A_BOLD);
        }
}
