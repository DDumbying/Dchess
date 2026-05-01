#include "tui/render.h"
#include "engine/movegen.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include <string.h>
#include <stdio.h>

/* Unicode chess symbols */
static const char *white_uni[6] = { "♙","♘","♗","♖","♕","♔" };
static const char *black_uni[6] = { "♟","♞","♝","♜","♛","♚" };

static const int start_count[12] = { 8,2,2,2,1,1, 8,2,2,2,1,1 };
static const int piece_val[6]    = { 1,3,3,5,9,0 };

/* Color pair IDs */
#define CP_LIGHT_SQ      1   /* normal light square */
#define CP_DARK_SQ       2   /* normal dark square  */
#define CP_W_ON_LIGHT    3
#define CP_W_ON_DARK     4
#define CP_B_ON_LIGHT    5
#define CP_B_ON_DARK     6
#define CP_BORDER        7
#define CP_LABEL         8
#define CP_INFO_HEAD     9
#define CP_INFO_VAL     10
#define CP_STATUS_OK    11
#define CP_STATUS_ERR   12
#define CP_HINT         13
#define CP_CMD          14
#define CP_MOVE_W       15
#define CP_MOVE_B       16
#define CP_TITLE        17
#define CP_LINK         18
#define CP_SHADOW       19
#define CP_CAP_W        20
#define CP_CAP_B        21
#define CP_CANVAS       22
/* Highlight squares */
#define CP_CHECK_SQ     23   /* king-in-check square — red bg */
#define CP_CHECK_PIECE  24   /* king piece on check square */
#define CP_LASTMV_LIGHT 25   /* last-move highlight on light sq — warm yellow */
#define CP_LASTMV_DARK  26   /* last-move highlight on dark  sq */
#define CP_W_ON_LMV_L   27   /* white piece on last-move light */
#define CP_W_ON_LMV_D   28   /* white piece on last-move dark  */
#define CP_B_ON_LMV_L   29   /* black piece on last-move light */
#define CP_B_ON_LMV_D   30   /* black piece on last-move dark  */

/* Custom color slots (only used when can_change_color()) */
#define COL_CANVAS     8    /* charcoal canvas */
#define COL_LIGHT_SQ   9    /* warm parchment */
#define COL_DARK_SQ   10    /* rich walnut brown */
#define COL_W_PIECE   11    /* cream white */
#define COL_B_PIECE   12    /* near-black espresso */
#define COL_CHROME    13    /* muted steel blue for borders/labels */
#define COL_TITLE_BG  14    /* dark teal title bar */
#define COL_SHADOW    15    /* near-black shadow */
#define COL_CHECK_BG  16    /* vivid red for check */
#define COL_CHECK_FG  17    /* bright yellow king on check */
#define COL_LMV_LIGHT 18    /* last-move tint on light sq */
#define COL_LMV_DARK  19    /* last-move tint on dark  sq */

void init_colors(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    if (can_change_color()) {
        /* Define custom colors (0-1000 scale) */
        init_color(COL_CANVAS,    120, 140, 130);  /* muted dark teal-gray */
        init_color(COL_LIGHT_SQ,  870, 820, 710);  /* warm parchment */
        init_color(COL_DARK_SQ,   360, 250, 150);  /* rich walnut */
        init_color(COL_W_PIECE,   990, 980, 940);  /* bright cream */
        init_color(COL_B_PIECE,   150, 300, 700);  /* deep blue — visible on both square colors */
        init_color(COL_CHROME,    480, 680, 700);  /* steel-blue chrome */
        init_color(COL_TITLE_BG,   80, 220, 220);  /* dark teal */
        init_color(COL_SHADOW,     60,  70,  65);  /* near-black */
        init_color(COL_CHECK_BG,  850, 150, 120);  /* vivid red */
        init_color(COL_CHECK_FG,  990, 900, 200);  /* bright gold */
        init_color(COL_LMV_LIGHT, 780, 820, 560);  /* yellow-green tint on light */
        init_color(COL_LMV_DARK,  400, 520, 200);  /* darker green tint on dark */

        /* Board squares */
        init_pair(CP_LIGHT_SQ,   COL_B_PIECE, COL_LIGHT_SQ);
        init_pair(CP_DARK_SQ,    COL_W_PIECE, COL_DARK_SQ);

        /* Pieces on normal squares */
        init_pair(CP_W_ON_LIGHT, COL_W_PIECE, COL_LIGHT_SQ);
        init_pair(CP_W_ON_DARK,  COL_W_PIECE, COL_DARK_SQ);
        init_pair(CP_B_ON_LIGHT, COL_B_PIECE, COL_LIGHT_SQ);
        init_pair(CP_B_ON_DARK,  COL_B_PIECE, COL_DARK_SQ);

        /* Check highlight */
        init_pair(CP_CHECK_SQ,    COL_CHECK_FG, COL_CHECK_BG);
        init_pair(CP_CHECK_PIECE, COL_CHECK_FG, COL_CHECK_BG);

        /* Last-move highlight squares */
        init_pair(CP_LASTMV_LIGHT, COL_B_PIECE, COL_LMV_LIGHT);
        init_pair(CP_LASTMV_DARK,  COL_W_PIECE, COL_LMV_DARK);
        init_pair(CP_W_ON_LMV_L,   COL_W_PIECE, COL_LMV_LIGHT);
        init_pair(CP_W_ON_LMV_D,   COL_W_PIECE, COL_LMV_DARK);
        init_pair(CP_B_ON_LMV_L,   COL_B_PIECE, COL_LMV_LIGHT);
        init_pair(CP_B_ON_LMV_D,   COL_B_PIECE, COL_LMV_DARK);

        /* UI chrome */
        init_pair(CP_BORDER,    COL_CHROME,     -1);
        init_pair(CP_LABEL,     COL_CHROME,     -1);
        init_pair(CP_INFO_HEAD, COLOR_YELLOW,   -1);
        init_pair(CP_INFO_VAL,  COLOR_WHITE,    -1);
        init_pair(CP_STATUS_OK, COLOR_GREEN,    -1);
        init_pair(CP_STATUS_ERR,COLOR_RED,      -1);
        init_pair(CP_HINT,      COL_CHROME,     -1);
        init_pair(CP_CMD,       COLOR_WHITE,    -1);
        init_pair(CP_MOVE_W,    COLOR_WHITE,    -1);
        init_pair(CP_MOVE_B,    COL_CHROME,     -1);
        init_pair(CP_TITLE,     COL_CHROME,     -1);  /* bold chrome in border */
        init_pair(CP_LINK,      COLOR_WHITE,    -1);
        init_pair(CP_SHADOW,    COL_SHADOW,     COL_SHADOW);
        init_pair(CP_CAP_W,     COLOR_WHITE,    -1);
        init_pair(CP_CAP_B,     COLOR_YELLOW,   -1);
        init_pair(CP_CANVAS,    COL_CANVAS,     COL_CANVAS);

    } else {
        /* Fallback for 8-color terminals */
        init_pair(CP_LIGHT_SQ,    COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_DARK_SQ,     COLOR_WHITE, COLOR_YELLOW);
        init_pair(CP_W_ON_LIGHT,  COLOR_BLUE,  COLOR_WHITE);
        init_pair(CP_W_ON_DARK,   COLOR_WHITE, COLOR_YELLOW);
        init_pair(CP_B_ON_LIGHT,  COLOR_RED,   COLOR_WHITE);
        init_pair(CP_B_ON_DARK,   COLOR_RED,   COLOR_YELLOW);
        init_pair(CP_CHECK_SQ,    COLOR_YELLOW,COLOR_RED);
        init_pair(CP_CHECK_PIECE, COLOR_YELLOW,COLOR_RED);
        init_pair(CP_LASTMV_LIGHT,COLOR_BLACK, COLOR_GREEN);
        init_pair(CP_LASTMV_DARK, COLOR_WHITE, COLOR_GREEN);
        init_pair(CP_W_ON_LMV_L,  COLOR_BLUE,  COLOR_GREEN);
        init_pair(CP_W_ON_LMV_D,  COLOR_WHITE, COLOR_GREEN);
        init_pair(CP_B_ON_LMV_L,  COLOR_RED,   COLOR_GREEN);
        init_pair(CP_B_ON_LMV_D,  COLOR_RED,   COLOR_GREEN);
        init_pair(CP_BORDER,     COLOR_CYAN,   -1);
        init_pair(CP_LABEL,      COLOR_CYAN,   -1);
        init_pair(CP_INFO_HEAD,  COLOR_YELLOW, -1);
        init_pair(CP_INFO_VAL,   COLOR_WHITE,  -1);
        init_pair(CP_STATUS_OK,  COLOR_GREEN,  -1);
        init_pair(CP_STATUS_ERR, COLOR_RED,    -1);
        init_pair(CP_HINT,       COLOR_BLACK,  -1);
        init_pair(CP_CMD,        COLOR_WHITE,  -1);
        init_pair(CP_MOVE_W,     COLOR_WHITE,  -1);
        init_pair(CP_MOVE_B,     COLOR_CYAN,   -1);
        init_pair(CP_TITLE,      COLOR_CYAN,  -1);
        init_pair(CP_LINK,       COLOR_WHITE,  -1);
        init_pair(CP_SHADOW,     COLOR_BLACK,  COLOR_BLACK);
        init_pair(CP_CAP_W,      COLOR_BLUE,   -1);
        init_pair(CP_CAP_B,      COLOR_RED,    -1);
        init_pair(CP_CANVAS,     COLOR_WHITE,  COLOR_BLUE);
    }
}

/* Helpers */
static int get_piece_at(const Position *pos, int sq) {
    for (int i = 0; i < 12; i++)
        if (GET_BIT(pos->bitboards[i], sq)) return i;
    return -1;
}

static void hline_win(WINDOW *w, int row, int col, int len, chtype ch) {
    for (int i = 0; i < len; i++) mvwaddch(w, row, col + i, ch);
}

static int popcount64(U64 b) {
    int c = 0; while (b) { c++; b &= b-1; } return c;
}

static void get_captured(const Position *pos,
                         int captured_w[6], int captured_b[6]) {
    for (int i = 0; i < 6; i++) {
        captured_w[i] = start_count[i]   - popcount64(pos->bitboards[i]);
        captured_b[i] = start_count[i+6] - popcount64(pos->bitboards[i+6]);
        if (captured_w[i] < 0) captured_w[i] = 0;
        if (captured_b[i] < 0) captured_b[i] = 0;
    }
}

/* Parse "e2e4" style move string → from/to squares, or -1 if invalid */
static void parse_move_sq(const char *mv, int *from, int *to) {
    *from = *to = -1;
    if (!mv || strlen(mv) < 4) return;
    if (mv[0]<'a'||mv[0]>'h'||mv[2]<'a'||mv[2]>'h') return;
    if (mv[1]<'1'||mv[1]>'8'||mv[3]<'1'||mv[3]>'8') return;
    *from = (mv[1]-'1')*8 + (mv[0]-'a');
    *to   = (mv[3]-'1')*8 + (mv[2]-'a');
}

/* Board grid drawing */
static void draw_board_grid(WINDOW *win, const Position *pos,
                            int start_row, int start_col,
                            int sq_h, int sq_w,
                            int last_from, int last_to) {

    /* Pre-compute check state */
    int w_in_check = is_in_check(pos, WHITE);
    int b_in_check = is_in_check(pos, BLACK);

    /* Find king squares */
    U64 wk_bb = pos->bitboards[K];
    U64 bk_bb = pos->bitboards[k];
    int wk_sq = wk_bb ? lsb(wk_bb) : -1;
    int bk_sq = bk_bb ? lsb(bk_bb) : -1;

    for (int rank = 7; rank >= 0; rank--) {
        for (int roff = 0; roff < sq_h; roff++) {
            int row    = start_row + (7 - rank) * sq_h + roff;
            int mid    = (roff == sq_h / 2);

            /* Rank label on the mid row */
            if (mid) {
                wattron(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
                mvwprintw(win, row, start_col, "%d ", rank + 1);
                wattroff(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
            } else {
                wattron(win, COLOR_PAIR(CP_HINT));
                mvwprintw(win, row, start_col, "  ");
                wattroff(win, COLOR_PAIR(CP_HINT));
            }

            for (int file = 0; file < 8; file++) {
                int sq    = rank * 8 + file;
                int piece = get_piece_at(pos, sq);
                int light = (rank + file) % 2 != 0;
                int col   = start_col + 2 + file * sq_w;

                /* Determine square type: check > last-move > normal */
                int is_check_sq = (sq == wk_sq && w_in_check) ||
                                  (sq == bk_sq && b_in_check);
                int is_lastmv   = (sq == last_from || sq == last_to) && !is_check_sq;

                /* Background color pair for the empty square */
                attr_t sq_bg;
                if (is_check_sq)
                    sq_bg = COLOR_PAIR(CP_CHECK_SQ);
                else if (is_lastmv)
                    sq_bg = COLOR_PAIR(light ? CP_LASTMV_LIGHT : CP_LASTMV_DARK);
                else
                    sq_bg = COLOR_PAIR(light ? CP_LIGHT_SQ : CP_DARK_SQ);

                if (mid && piece >= 0) {
                    int is_white = (piece < 6);
                    int idx      = piece % 6;
                    const char *sym = is_white ? white_uni[idx] : black_uni[idx];
                    int sym_w = 2;
                    int lpad  = (sq_w - sym_w) / 2;
                    int rpad  = sq_w - sym_w - lpad;

                    /* Piece color pair */
                    attr_t pc;
                    if (is_check_sq) {
                        pc = COLOR_PAIR(CP_CHECK_PIECE) | A_BOLD;
                    } else if (is_lastmv) {
                        if (is_white)
                            pc = COLOR_PAIR(light ? CP_W_ON_LMV_L : CP_W_ON_LMV_D) | A_BOLD;
                        else
                            pc = COLOR_PAIR(light ? CP_B_ON_LMV_L : CP_B_ON_LMV_D) | A_BOLD;
                    } else {
                        if (is_white)
                            pc = COLOR_PAIR(light ? CP_W_ON_LIGHT : CP_W_ON_DARK) | A_BOLD;
                        else
                            pc = COLOR_PAIR(light ? CP_B_ON_LIGHT : CP_B_ON_DARK) | A_BOLD;
                    }

                    wattron(win, sq_bg);
                    for (int c = 0; c < lpad; c++) mvwaddch(win, row, col+c, ' ');
                    wattroff(win, sq_bg);

                    wattron(win, pc);
                    mvwprintw(win, row, col+lpad, "%s", sym);
                    wattroff(win, pc);

                    wattron(win, sq_bg);
                    for (int c = 0; c < rpad; c++)
                        mvwaddch(win, row, col+lpad+sym_w+c, ' ');
                    wattroff(win, sq_bg);

                } else {
                    wattron(win, sq_bg);
                    for (int c = 0; c < sq_w; c++) mvwaddch(win, row, col+c, ' ');
                    wattroff(win, sq_bg);
                }
            }
        }
    }

    /* File labels */
    int lrow = start_row + 8 * sq_h;
    wattron(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
    for (int f = 0; f < 8; f++)
        mvwprintw(win, lrow, start_col+2 + f*sq_w + sq_w/2, "%c", 'a'+f);
    wattroff(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
}

/* draw_board  — sizes and centers board, draws shadow + grid */
static void draw_board(WINDOW *win, const TUIState *state) {
    int wh, ww;
    getmaxyx(win, wh, ww);

    int avail_h = wh - 9;   /* top border + title + bottom status rows */
    int avail_w = ww - 4;

    int sq_h = avail_h / 8;
    if (sq_h < 1) sq_h = 1;
    if (sq_h > 8) sq_h = 8;

    int sq_w = sq_h * 2;
    if (sq_w < 4) sq_w = 4;

    int board_w = 2 + 8 * sq_w;
    while (board_w > avail_w && sq_w > 4) {
        sq_w--;
        board_w = 2 + 8 * sq_w;
    }
    sq_h = sq_w / 2;
    if (sq_h < 1) sq_h = 1;

    int board_h  = 8 * sq_h + 1;
    int start_col = (ww - board_w) / 2;
    int start_row = 2 + (avail_h - board_h) / 2;
    if (start_col < 2) start_col = 2;
    if (start_row < 2) start_row = 2;

    /* Parse last move for highlighting */
    int last_from = -1, last_to = -1;
    if (state->move_count > 0)
        parse_move_sq(state->move_history[state->move_count - 1],
                      &last_from, &last_to);

    draw_board_grid(win, &state->pos, start_row, start_col,
                    sq_h, sq_w, last_from, last_to);
}

/* Status bar */
static void draw_board_status(WINDOW *win, const TUIState *state) {
    int wh, ww;
    getmaxyx(win, wh, ww);

    int is_err = (strncmp(state->status, "Illegal", 7) == 0 ||
                  strncmp(state->status, "Bad",     3) == 0 ||
                  strncmp(state->status, "Unknown", 7) == 0);

    wattron(win, is_err ? (COLOR_PAIR(CP_STATUS_ERR)|A_BOLD)
                        : (COLOR_PAIR(CP_STATUS_OK) |A_BOLD));
    mvwprintw(win, wh-4, 2, " %-*.*s ", ww-6, ww-6, state->status);
    wattroff(win, is_err ? (COLOR_PAIR(CP_STATUS_ERR)|A_BOLD)
                         : (COLOR_PAIR(CP_STATUS_OK) |A_BOLD));

    /* Check indicator in status line */
    int in_check = is_in_check(&state->pos, state->pos.side);
    if (in_check) {
        wattron(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
        mvwprintw(win, wh-4, ww-14, " !! CHECK !! ");
        wattroff(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
    }

    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, wh-3, 2, " %-*.*s ", ww-6, ww-6,
        "e2e4=move  go=engine  new=reset  flip=sides  depth N  quit");
    wattroff(win, COLOR_PAIR(CP_HINT));

    char info[256];
    snprintf(info, sizeof(info), " %s to move   depth:%d   eval:%s ",
             state->pos.side == WHITE ? "♔ White" : "♚ Black",
             state->engine_depth, state->last_eval);
    wattron(win, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);
    mvwprintw(win, wh-2, 2, "%-*.*s", ww-4, ww-4, info);
    wattroff(win, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);
}

/* Info panel */
static void draw_captured_row(WINDOW *win, int row, int col,
                               const int cap[6], int is_white_caps, int ww) {
    const char **syms = is_white_caps ? white_uni : black_uni;
    attr_t attr = is_white_caps ? (COLOR_PAIR(CP_CAP_W)|A_BOLD)
                                : (COLOR_PAIR(CP_CAP_B)|A_BOLD);
    int x = col, any = 0;
    for (int i = 0; i < 6 && x < ww-1; i++) {
        for (int j = 0; j < cap[i] && x < ww-2; j++) {
            wattron(win, attr);
            mvwprintw(win, row, x, "%s", syms[i]);
            wattroff(win, attr);
            x += 2; any = 1;
        }
    }
    if (!any) {
        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, row, col, "none");
        wattroff(win, COLOR_PAIR(CP_HINT));
    }
}

static void draw_info(WINDOW *win, const TUIState *state) {
    wclear(win);
    int wh, ww;
    getmaxyx(win, wh, ww);

    wattron(win, COLOR_PAIR(CP_BORDER));
    box(win, ACS_VLINE, ACS_HLINE);
    wattroff(win, COLOR_PAIR(CP_BORDER));

    /* Title — plain text in border line */
    wattron(win, COLOR_PAIR(CP_TITLE)|A_BOLD);
    mvwprintw(win, 0, (ww-6)/2, " INFO ");
    wattroff(win, COLOR_PAIR(CP_TITLE)|A_BOLD);

    int row = 2;

    /* GAME */
    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvwprintw(win, row++, 2, "GAME");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    const char *eng_s = state->engine_side == WHITE ? "White" :
                        state->engine_side == BLACK ? "Black" : "None";
    wattron(win, COLOR_PAIR(CP_INFO_VAL));
    mvwprintw(win, row++, 2, "depth : %d",  state->engine_depth);
    mvwprintw(win, row++, 2, "eval  : %s",  state->last_eval);
    mvwprintw(win, row++, 2, "side  : %s",
              state->pos.side == WHITE ? "White" : "Black");
    mvwprintw(win, row++, 2, "engine: %s",  eng_s);
    wattroff(win, COLOR_PAIR(CP_INFO_VAL));

    /* Check indicator in info panel */
    if (is_in_check(&state->pos, state->pos.side)) {
        wattron(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
        mvwprintw(win, row++, 2, "** CHECK **");
        wattroff(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
    } else {
        row++;
    }

    /* MOVES */
    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvwprintw(win, row++, 2, "MOVES");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, row++, 2, "  # W    B");
    wattroff(win, COLOR_PAIR(CP_HINT));

    int cap_section_h = 10;
    int history_rows  = wh - row - cap_section_h;
    if (history_rows < 1) history_rows = 1;

    int total_pairs = (state->move_count + 1) / 2;
    int start_pair  = total_pairs - history_rows;
    if (start_pair < 0) start_pair = 0;

    for (int p = start_pair; p < total_pairs && row < wh-cap_section_h; p++) {
        int w_idx = p*2, b_idx = p*2+1;
        char w_mv[9] = "-", b_mv[9] = "-";
        if (w_idx < state->move_count) strncpy(w_mv, state->move_history[w_idx], 8);
        if (b_idx < state->move_count) strncpy(b_mv, state->move_history[b_idx], 8);

        /* Highlight the most recent move pair */
        int is_latest = (p == total_pairs - 1);

        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, row, 2, "%3d.", p+1);
        wattroff(win, COLOR_PAIR(CP_HINT));

        wattron(win, is_latest ? (COLOR_PAIR(CP_STATUS_OK)|A_BOLD)
                               : (COLOR_PAIR(CP_MOVE_W)|A_BOLD));
        mvwprintw(win, row, 7, "%-4s", w_mv);
        wattroff(win, is_latest ? (COLOR_PAIR(CP_STATUS_OK)|A_BOLD)
                                : (COLOR_PAIR(CP_MOVE_W)|A_BOLD));

        wattron(win, is_latest ? (COLOR_PAIR(CP_STATUS_OK))
                               : (COLOR_PAIR(CP_MOVE_B)));
        mvwprintw(win, row, 12, "%-4s", b_mv);
        wattroff(win, is_latest ? (COLOR_PAIR(CP_STATUS_OK))
                                : (COLOR_PAIR(CP_MOVE_B)));
        row++;
    }

    if (state->move_count == 0) {
        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, row, 2, "(no moves)");
        wattroff(win, COLOR_PAIR(CP_HINT));
    }

    /* CAPTURED */
    int cap_top = wh - cap_section_h;
    wattron(win, COLOR_PAIR(CP_BORDER));
    mvwaddch(win, cap_top, 0, ACS_LTEE);
    hline_win(win, cap_top, 1, ww-2, ACS_HLINE);
    mvwaddch(win, cap_top, ww-1, ACS_RTEE);
    wattroff(win, COLOR_PAIR(CP_BORDER));
    cap_top++;

    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvwprintw(win, cap_top++, 2, "CAPTURED");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    int cap_w[6], cap_b[6];
    get_captured(&state->pos, cap_w, cap_b);

    int mat_w = 0, mat_b = 0;
    for (int i = 0; i < 5; i++) {
        mat_w += cap_b[i] * piece_val[i];
        mat_b += cap_w[i] * piece_val[i];
    }
    int adv = mat_w - mat_b;

    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, cap_top++, 2, "W took:");
    wattroff(win, COLOR_PAIR(CP_HINT));
    draw_captured_row(win, cap_top++, 2, cap_b, 0, ww);

    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, cap_top++, 2, "B took:");
    wattroff(win, COLOR_PAIR(CP_HINT));
    draw_captured_row(win, cap_top++, 2, cap_w, 1, ww);

    wattron(win, adv != 0 ? (COLOR_PAIR(CP_STATUS_OK)|A_BOLD)
                          : (COLOR_PAIR(CP_INFO_VAL)));
    if      (adv > 0) mvwprintw(win, cap_top++, 2, "+%d White", adv);
    else if (adv < 0) mvwprintw(win, cap_top++, 2, "+%d Black", -adv);
    else              mvwprintw(win, cap_top++, 2, "Even");
    wattroff(win, adv != 0 ? (COLOR_PAIR(CP_STATUS_OK)|A_BOLD)
                           : (COLOR_PAIR(CP_INFO_VAL)));

    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, wh-2, 2, "DDumbying.Org");
    wattroff(win, COLOR_PAIR(CP_HINT));

    wnoutrefresh(win);
}

/* Command bar */
static void draw_cmd(WINDOW *win) {
    wclear(win);
    wattron(win, COLOR_PAIR(CP_BORDER));
    box(win, ACS_VLINE, ACS_HLINE);
    wattroff(win, COLOR_PAIR(CP_BORDER));
    wattron(win, COLOR_PAIR(CP_CMD)|A_BOLD);
    mvwprintw(win, 1, 2, "command: ");
    wattroff(win, COLOR_PAIR(CP_CMD)|A_BOLD);
    wnoutrefresh(win);
}

/* render_all  — top-level entry point from tui.c */
void render_all(WINDOW *board_win, WINDOW *info_win,
                WINDOW *cmd_win,   const TUIState *state) {

    wclear(board_win);
    int bh, bw;
    getmaxyx(board_win, bh, bw);

    /* Window border */
    wattron(board_win, COLOR_PAIR(CP_BORDER));
    box(board_win, ACS_VLINE, ACS_HLINE);
    wattroff(board_win, COLOR_PAIR(CP_BORDER));

    /* Title — plain text in the border line, no filled strip */
    wattron(board_win, COLOR_PAIR(CP_TITLE)|A_BOLD);
    mvwprintw(board_win, 0, 2, " Dchess ");
    wattroff(board_win, COLOR_PAIR(CP_TITLE)|A_BOLD);
    wattron(board_win, COLOR_PAIR(CP_LINK)|A_BOLD);
    const char *brand = " github.com/DDumbying ";
    int brand_col = bw - (int)strlen(brand) - 1;
    if (brand_col > 10)
        mvwprintw(board_win, 0, brand_col, "%s", brand);
    wattroff(board_win, COLOR_PAIR(CP_LINK)|A_BOLD);

    /* Separator above status */
    wattron(board_win, COLOR_PAIR(CP_BORDER));
    mvwaddch(board_win, bh-5, 0, ACS_LTEE);
    hline_win(board_win, bh-5, 1, bw-2, ACS_HLINE);
    mvwaddch(board_win, bh-5, bw-1, ACS_RTEE);
    wattroff(board_win, COLOR_PAIR(CP_BORDER));

    draw_board(board_win, state);
    draw_board_status(board_win, state);
    wnoutrefresh(board_win);

    if (info_win) draw_info(info_win, state);
    draw_cmd(cmd_win);
}
