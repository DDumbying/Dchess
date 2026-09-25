/* render.c — Dchess TUI rendering
 * Rendering contract (per the spec):
 *   • Each square: SQ_W=5 cols × SQ_H=2 rows, fixed.
 *   • Row 0 of square: blank background fill
 *   • Row 1 of square: piece centered at col+2 (mid of 5)
 *   • Layers: (1) square bg  (2) move highlight  (3) cursor/selection  (4) piece
 *   • Colors strictly separated: square pair = bg only, piece pair = fg only
 *   • mvadd_wch() for all Unicode glyphs (correct wide-char width)
 */

#include "tui/render.h"
#include "tui/colors.h"
#include "tui/piece_art.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include "utils/theme.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>

/* Unicode pieces  */
static const wchar_t PIECE_GLYPH[12] = {
    0x2659, 0x2658, 0x2657, 0x2656, 0x2655, 0x2654,  /* ♙♘♗♖♕♔ white */
    0x265F, 0x265E, 0x265D, 0x265C, 0x265B, 0x265A   /* ♟♞♝♜♛♚ black */
};

/* Actual rendered width of a chess glyph in this terminal.
 * Most fonts treat U+2654-U+265F as narrow (1 cell); a few CJK fonts
 * render them as wide (2 cells). We detect this once at startup. */
static int GLYPH_W = 1;   /* default; overridden by init_glyph_width() */

static void init_glyph_width(void)
{
    int w = wcwidth(PIECE_GLYPH[0]);   /* check ♙ */
    GLYPH_W = (w == 2) ? 2 : 1;
}

/* Square dimensions — computed at render time to fill available space ── */
#define SQ_W_MIN 5
#define SQ_H_MIN 2



/* Foregrounds are derived from the theme's background rather than
 * hard-coded, so no theme can pick a background that renders its own
 * foreground invisible. */

/* Which of the 8 ANSI colors read as light backgrounds? */
static int fb_is_light(int bg)
{
    return bg == COLOR_WHITE || bg == COLOR_YELLOW || bg == COLOR_CYAN;
}

static int fb_plain_fg(int bg)
{
    return fb_is_light(bg) ? COLOR_BLACK : COLOR_WHITE;
}

static int fb_white_pc_fg(int bg)
{
    return fb_is_light(bg) ? COLOR_BLUE : COLOR_WHITE;
}

/* Red reads as "black piece" elsewhere in this file. */
static int fb_black_pc_fg(int bg)
{
    return (bg == COLOR_RED || bg == COLOR_MAGENTA) ? COLOR_BLACK : COLOR_RED;
}

static void init_palette_256(const Theme *t)
{
    int bg = t->bg;

    init_pair(CP_LIGHT,   t->sq_light, t->sq_light);
    init_pair(CP_DARK,    t->sq_dark,  t->sq_dark);
    init_pair(CP_W_LIGHT, t->pc_white, t->sq_light);
    init_pair(CP_W_DARK,  t->pc_white, t->sq_dark);
    init_pair(CP_B_LIGHT, t->pc_black, t->sq_light);
    init_pair(CP_B_DARK,  t->pc_black, t->sq_dark);

    init_pair(CP_CURSOR,       t->pc_black, t->cursor);
    init_pair(CP_CURSOR_PC,    t->pc_white, t->cursor);
    init_pair(CP_CURSOR_PC_B,  t->pc_black, t->cursor);
    init_pair(CP_SEL,          t->pc_black, t->sel);
    init_pair(CP_SEL_PC,       t->pc_white, t->sel);
    init_pair(CP_SEL_PC_B,     t->pc_black, t->sel);
    init_pair(CP_MOVE_HI,      t->pc_black, t->movehi);
    init_pair(CP_MOVE_HI_PC,   t->pc_white, t->movehi);
    init_pair(CP_MOVE_HI_PC_B, t->pc_black, t->movehi);
    init_pair(CP_CHECK_SQ,     t->pc_black, t->check);
    init_pair(CP_CHECK_PC,     t->pc_white, t->check);
    init_pair(CP_CHECK_PC_B,   t->pc_black, t->check);

    init_pair(CP_LMVL,   t->lm_light, t->lm_light);
    init_pair(CP_LMVD,   t->lm_dark,  t->lm_dark);
    init_pair(CP_W_LMVL, t->pc_white, t->lm_light);
    init_pair(CP_W_LMVD, t->pc_white, t->lm_dark);
    init_pair(CP_B_LMVL, t->pc_black, t->lm_light);
    init_pair(CP_B_LMVD, t->pc_black, t->lm_dark);

    init_pair(CP_BORDER,     t->line, bg);
    init_pair(CP_TITLE,      t->fg,   bg);
    init_pair(CP_LINK,       t->dim,  bg);
    init_pair(CP_LABEL,      t->dim,  bg);
    init_pair(CP_INFO_HEAD,  t->acc_board, bg);
    init_pair(CP_INFO_VAL,   t->fg,   bg);
    init_pair(CP_STATUS_OK,  t->ok,   bg);
    init_pair(CP_STATUS_ERR, t->err,  bg);
    init_pair(CP_HINT,       t->dim,  bg);
    init_pair(CP_CMD,        t->fg,   bg);
    init_pair(CP_MOVE_W,     t->fg,   bg);
    init_pair(CP_MOVE_B,     t->dim,  bg);
    init_pair(CP_CAP_W,      t->fg,   bg);
    init_pair(CP_CAP_B,      t->dim,  bg);
    init_pair(CP_CANVAS,     t->fg,   bg);
    init_pair(CP_SHADOW,     t->shadow, bg);
    init_pair(CP_FRAME,      t->line, bg);

    init_pair(CP_ACC_BOARD,  t->acc_board,  bg);
    init_pair(CP_ACC_EVAL,   t->acc_eval,   bg);
    init_pair(CP_ACC_CLOCK,  t->acc_clock,  bg);
    init_pair(CP_ACC_MOVES,  t->acc_moves,  bg);
    init_pair(CP_ACC_ENGINE, t->acc_engine, bg);
    init_pair(CP_TRACK,      t->track, t->track);
    for (int i = 0; i < THEME_RAMP; i++)
        init_pair(CP_RAMP_BASE + i, t->ramp[i], bg);
}

static void init_palette_8(const Theme *t)
{
    static const int RAMP8[THEME_RAMP] = {
        COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_YELLOW,
        COLOR_YELLOW, COLOR_YELLOW, COLOR_RED, COLOR_RED,
    };

    init_pair(CP_LIGHT,      COLOR_BLACK,  COLOR_WHITE);
    init_pair(CP_DARK,       COLOR_WHITE,  COLOR_BLACK);
    init_pair(CP_W_LIGHT,    COLOR_YELLOW, COLOR_WHITE);
    init_pair(CP_W_DARK,     COLOR_YELLOW, COLOR_BLACK);
    init_pair(CP_B_LIGHT,    COLOR_RED,    COLOR_WHITE);
    init_pair(CP_B_DARK,     COLOR_RED,    COLOR_BLACK);
    init_pair(CP_CURSOR,       fb_plain_fg(t->fb_cursor_bg),    t->fb_cursor_bg);
    init_pair(CP_CURSOR_PC,    fb_white_pc_fg(t->fb_cursor_bg), t->fb_cursor_bg);
    init_pair(CP_CURSOR_PC_B,  fb_black_pc_fg(t->fb_cursor_bg), t->fb_cursor_bg);
    init_pair(CP_SEL,          fb_plain_fg(t->fb_sel_bg),       t->fb_sel_bg);
    init_pair(CP_SEL_PC,       fb_white_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_SEL_PC_B,     fb_black_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_MOVE_HI,      fb_plain_fg(t->fb_movehi_bg),    t->fb_movehi_bg);
    init_pair(CP_MOVE_HI_PC,   fb_white_pc_fg(t->fb_movehi_bg), t->fb_movehi_bg);
    init_pair(CP_MOVE_HI_PC_B, fb_black_pc_fg(t->fb_movehi_bg), t->fb_movehi_bg);
    init_pair(CP_CHECK_SQ,     fb_plain_fg(t->fb_check_bg),     t->fb_check_bg);
    init_pair(CP_CHECK_PC,     fb_white_pc_fg(t->fb_check_bg),  t->fb_check_bg);
    init_pair(CP_CHECK_PC_B,   fb_black_pc_fg(t->fb_check_bg),  t->fb_check_bg);
    init_pair(CP_LMVL,       fb_plain_fg(t->fb_sel_bg),       t->fb_sel_bg);
    init_pair(CP_LMVD,       fb_plain_fg(t->fb_sel_bg),       t->fb_sel_bg);
    init_pair(CP_W_LMVL,     fb_white_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_W_LMVD,     fb_white_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_B_LMVL,     fb_black_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_B_LMVD,     fb_black_pc_fg(t->fb_sel_bg),    t->fb_sel_bg);
    init_pair(CP_BORDER,     t->fb_accent, -1);
    init_pair(CP_TITLE,      t->fb_accent, -1);
    init_pair(CP_LINK,       COLOR_WHITE,  -1);
    init_pair(CP_LABEL,      t->fb_accent, -1);
    init_pair(CP_INFO_HEAD,  COLOR_YELLOW, -1);
    init_pair(CP_INFO_VAL,   COLOR_WHITE,  -1);
    init_pair(CP_STATUS_OK,  COLOR_GREEN,  -1);
    init_pair(CP_STATUS_ERR, COLOR_RED,    -1);
    init_pair(CP_HINT,       t->fb_accent, -1);
    init_pair(CP_CMD,        COLOR_WHITE,  -1);
    init_pair(CP_MOVE_W,     COLOR_WHITE,  -1);
    init_pair(CP_MOVE_B,     t->fb_accent, -1);
    init_pair(CP_CAP_W,      COLOR_BLUE,   -1);
    init_pair(CP_CAP_B,      COLOR_RED,    -1);
    init_pair(CP_CANVAS,     COLOR_WHITE,  -1);
    init_pair(CP_SHADOW,     COLOR_BLACK,  -1);
    init_pair(CP_FRAME,      t->fb_accent, -1);
    init_pair(CP_ACC_BOARD,  COLOR_YELLOW,  -1);
    init_pair(CP_ACC_EVAL,   COLOR_GREEN,   -1);
    init_pair(CP_ACC_CLOCK,  COLOR_YELLOW,  -1);
    init_pair(CP_ACC_MOVES,  COLOR_MAGENTA, -1);
    init_pair(CP_ACC_ENGINE, COLOR_CYAN,    -1);
    init_pair(CP_TRACK,      COLOR_BLACK, COLOR_BLACK);
    for (int i = 0; i < THEME_RAMP; i++)
        init_pair(CP_RAMP_BASE + i, RAMP8[i], -1);
}

void init_colors(int theme)
{
    init_glyph_width();
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    const Theme *t = theme_get(theme);
    if (COLORS >= 256) init_palette_256(t);
    else               init_palette_8(t);

    bkgd(COLOR_PAIR(CP_CANVAS));
}

/* Internal helpers  */
static int piece_at(const Position *pos, int sq)
{
    for (int i = 0; i < 12; i++)
        if (GET_BIT(pos->bitboards[i], sq)) return i;
    return -1;
}

/* printf truncated at the right border. Plain mvwprintw() does not clip:
 * ncurses wraps an over-long string onto the next line of the same
 * window, silently overwriting it. */
static void mvw_clip(WINDOW *win, int row, int col, const char *fmt, ...)
{
    int wh, ww;
    getmaxyx(win, wh, ww);
    if (row < 0 || row >= wh || col < 0 || col >= ww - 1) return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    mvwprintw(win, row, col, "%.*s", ww - col - 1, buf);
}

static void hfill(WINDOW *w, int r, int c, int len, chtype ch)
{
    for (int i = 0; i < len; i++) mvwaddch(w, r, c + i, ch);
}

static void parse_last_move(const TUIState *s, int *from, int *to)
{
    *from = *to = -1;
    if (s->game.move_count < 1) return;
    Move m = s->game.move_made[s->game.move_count - 1];
    *from = FROM(m);
    *to   = TO(m);
}

static int popcount64(U64 b)
{
    int c = 0; while (b) { c++; b &= b-1; } return c;
}
static const int START_CNT[12] = { 8,2,2,2,1,1, 8,2,2,2,1,1 };
static const int PC_VAL[6]     = { 1,3,3,5,9,0 };

static void captured_counts(const Position *pos,
                             int cw[6], int cb[6])
{
    for (int i = 0; i < 6; i++) {
        cw[i] = START_CNT[i]   - popcount64(pos->bitboards[i]);
        cb[i] = START_CNT[i+6] - popcount64(pos->bitboards[i+6]);
        if (cw[i] < 0) cw[i] = 0;
        if (cb[i] < 0) cb[i] = 0;
    }
}

/* Write one chess glyph. When the font renders it as 1 cell (narrow),
 * we write the glyph then fill the second reserved cell with a background
 * space so the square background stays clean. */
static void put_glyph(WINDOW *win, int r, int c, int piece, attr_t attr)
{
    cchar_t cc;
    wchar_t ws[2] = { PIECE_GLYPH[piece], L'\0' };
    attr_t style = attr & (A_BOLD | A_DIM | A_UNDERLINE | A_REVERSE);
    setcchar(&cc, ws, style, (short)PAIR_NUMBER(attr), NULL);
    wattron(win, attr);
    mvwadd_wch(win, r, c, &cc);
    if (GLYPH_W == 1) {
        /* Font treats this glyph as narrow — fill the gap cell */
        wattroff(win, attr);
        wattron(win, attr & ~(A_BOLD | A_DIM));
        mvwaddch(win, r, c + 1, ' ');
    }
    wattroff(win, attr);
}

/* draw_square
 * Draws one SQ_W × SQ_H square at window coordinates (row, col).
 * Layout (SQ_W=5, SQ_H=2):
 *   row+0:  "     "    ← sq_attr (background fill)
 *   row+1:  " ♙♙ "    ← sq_attr padding + pc_attr glyph (2 cells) + sq_attr padding
 *                         piece centered at col+1 (lpad=1, glyph=2, rpad=2)
 * sq_attr : color pair for background cells  (fg == bg on empty squares)
 * pc_attr : color pair for the glyph         (proper fg on same bg)
 * piece   : 0-11 index, or -1 for empty
 * dot     : draw a subtle "•" indicator for legal-move destinations
 */
/* Paints only the silhouette cells, so the square background shows
 * through the gaps and highlights still read. */
static void draw_piece_art(WINDOW *win, int row, int col,
                           int sq_h, int sq_w,
                           const PieceArtTier *tier, int piece,
                           attr_t sq_attr, attr_t pc_attr)
{
    int top  = row + (sq_h - tier->rows) / 2;
    int left = col + (sq_w - tier->cols) / 2;

    for (int r = 0; r < tier->rows; r++) {
        const wchar_t *line = piece_art_row(tier, piece, r);
        if (!line) continue;

        for (int c = 0; c < tier->cols && line[c]; c++) {
            if (line[c] == L' ') continue;   /* let the background show */

            cchar_t cc;
            wchar_t ws[2] = { line[c], L'\0' };
            attr_t style = pc_attr & (A_BOLD | A_DIM | A_UNDERLINE | A_REVERSE);
            setcchar(&cc, ws, style, (short)PAIR_NUMBER(pc_attr), NULL);
            mvwadd_wch(win, top + r, left + c, &cc);
        }
    }
    (void)sq_attr;
}

static void draw_square(WINDOW *win,
                        int row, int col,
                        int sq_h, int sq_w,
                        int piece,
                        attr_t sq_attr, attr_t pc_attr,
                        int dot)
{
    /* NULL below the smallest tier: fall back to the single glyph. */
    const PieceArtTier *tier = piece_art_for_square(sq_h, sq_w);

    if (piece >= 0 && tier) {
        /* Background first, silhouette over it. */
        wattron(win, sq_attr);
        for (int roff = 0; roff < sq_h; roff++)
            hfill(win, row + roff, col, sq_w, ' ');
        wattroff(win, sq_attr);

        draw_piece_art(win, row, col, sq_h, sq_w, tier, piece, sq_attr, pc_attr);
        return;
    }

    /* Non-middle rows — pure background */
    int mid = sq_h / 2;
    wattron(win, sq_attr);
    for (int roff = 0; roff < sq_h; roff++)
        if (roff != mid) hfill(win, row+roff, col, sq_w, ' ');
    wattroff(win, sq_attr);

    /* Middle row — piece centered */
    int lpad = (sq_w - GLYPH_W) / 2;
    int rpad = sq_w - GLYPH_W - lpad;
    int r = row + mid;

    wattron(win, sq_attr);
    hfill(win, r, col, lpad, ' ');
    wattroff(win, sq_attr);

    if (piece >= 0) {
        put_glyph(win, r, col+lpad, piece, pc_attr);
    } else if (dot) {
        wattron(win, pc_attr);
        mvwaddch(win, r, col + sq_w/2, ACS_BULLET);
        wattroff(win, pc_attr);
        wattron(win, sq_attr);
        mvwaddch(win, r, col + sq_w/2 + 1, ' ');
        wattroff(win, sq_attr);
    } else {
        wattron(win, sq_attr);
        hfill(win, r, col+lpad, GLYPH_W, ' ');
        wattroff(win, sq_attr);
    }

    wattron(win, sq_attr);
    hfill(win, r, col+lpad+GLYPH_W, rpad, ' ');
    wattroff(win, sq_attr);
}

/* Drawn before the squares, one cell outside the grid. */
static void draw_board_frame(WINDOW *win, int start_row, int start_col,
                             int sq_h, int sq_w)
{
    int h = 8 * sq_h;
    int w = 8 * sq_w;
    int top = start_row - 1, bottom = start_row + h;
    int left = start_col - 1, right = start_col + w;

    wattron(win, COLOR_PAIR(CP_FRAME));

    mvwaddch(win, top,    left,  ACS_ULCORNER);
    mvwaddch(win, top,    right, ACS_URCORNER);
    mvwaddch(win, bottom, left,  ACS_LLCORNER);
    mvwaddch(win, bottom, right, ACS_LRCORNER);

    for (int c = 0; c < w; c++) {
        mvwaddch(win, top,    left + 1 + c, ACS_HLINE);
        mvwaddch(win, bottom, left + 1 + c, ACS_HLINE);
    }
    for (int r = 0; r < h; r++) {
        mvwaddch(win, start_row + r, left,  ACS_VLINE);
        mvwaddch(win, start_row + r, right, ACS_VLINE);
    }

    wattroff(win, COLOR_PAIR(CP_FRAME));
}

static void draw_board_grid(WINDOW *win, const TUIState *state,
                            int start_row, int start_col,
                            int sq_h, int sq_w, int framed)
{
    const Position *pos = &state->game.pos;
    int flipped = (state->view_side == BLACK); /* Black at bottom when flipped */

    int w_chk = is_in_check(pos, WHITE);
    int b_chk = is_in_check(pos, BLACK);
    int wk_sq = pos->bitboards[K] ? lsb(pos->bitboards[K]) : -1;
    int bk_sq = pos->bitboards[k] ? lsb(pos->bitboards[k]) : -1;

    int lm_from, lm_to;
    parse_last_move(state, &lm_from, &lm_to);

    if (framed) draw_board_frame(win, start_row, start_col, sq_h, sq_w);

    for (int rank = 7; rank >= 0; rank--) {
        /* When flipped, rank 0 (white's back rank) is at the top */
        int drow = flipped ? rank : (7 - rank);
        int base = start_row + drow * sq_h;

        /* Rank label */
        wattron(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
        mvwprintw(win, base + sq_h/2, start_col - (framed ? 3 : 2), "%d", rank + 1);
        wattroff(win, COLOR_PAIR(CP_LABEL) | A_BOLD);

        for (int file = 0; file < 8; file++) {
            int dfile = flipped ? (7 - file) : file;
            int sq    = rank * 8 + dfile;
            int piece = piece_at(pos, sq);
            int light = (rank + dfile) % 2 != 0;

            int is_check  = (sq == wk_sq && w_chk) || (sq == bk_sq && b_chk);
            int is_cursor = (state->cursor_row == drow &&
                             state->cursor_col == file);
            int is_sel    = (state->selected &&
                             state->sel_row == drow &&
                             state->sel_col == file);
            int is_movehi = (state->selected &&
                             !is_cursor && !is_sel &&
                             state->highlight[drow][file]);
            int is_lmv    = (!is_check && !is_cursor && !is_sel && !is_movehi &&
                             (sq == lm_from || sq == lm_to));

            attr_t sq_attr;
            if      (is_check)  sq_attr = COLOR_PAIR(CP_CHECK_SQ);
            else if (is_sel)    sq_attr = COLOR_PAIR(CP_SEL);
            else if (is_cursor) sq_attr = COLOR_PAIR(CP_CURSOR);
            else if (is_movehi) sq_attr = COLOR_PAIR(CP_MOVE_HI);
            else if (is_lmv)    sq_attr = COLOR_PAIR(light ? CP_LMVL : CP_LMVD);
            else                sq_attr = COLOR_PAIR(light ? CP_LIGHT : CP_DARK);

            attr_t pc_attr = sq_attr;
            if (piece >= 0) {
                int iw = (piece < 6);
                if      (is_check)  pc_attr = COLOR_PAIR(iw ? CP_CHECK_PC   : CP_CHECK_PC_B)   | A_BOLD;
                else if (is_sel)    pc_attr = COLOR_PAIR(iw ? CP_SEL_PC     : CP_SEL_PC_B)     | A_BOLD;
                else if (is_cursor) pc_attr = COLOR_PAIR(iw ? CP_CURSOR_PC  : CP_CURSOR_PC_B)  | A_BOLD;
                else if (is_movehi) pc_attr = COLOR_PAIR(iw ? CP_MOVE_HI_PC : CP_MOVE_HI_PC_B) | A_BOLD;
                else if (is_lmv) {
                    if (iw) pc_attr = COLOR_PAIR(light ? CP_W_LMVL : CP_W_LMVD) | A_BOLD;
                    else    pc_attr = COLOR_PAIR(light ? CP_B_LMVL : CP_B_LMVD) | A_BOLD;
                } else {
                    if (iw) pc_attr = COLOR_PAIR(light ? CP_W_LIGHT : CP_W_DARK) | A_BOLD;
                    else    pc_attr = COLOR_PAIR(light ? CP_B_LIGHT : CP_B_DARK) | A_BOLD;
                }
            }

            int dot = (is_movehi && piece < 0);
            int col = start_col + file * sq_w;
            draw_square(win, base, col, sq_h, sq_w, piece, sq_attr, pc_attr, dot);
        }
    }

    /* a-h left-to-right, h-a when flipped. */
    int lr = start_row + 8 * sq_h + (framed ? 1 : 0);
    wattron(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
    for (int f = 0; f < 8; f++) {
        char label = flipped ? ('h' - f) : ('a' + f);
        mvwprintw(win, lr, start_col + f * sq_w + sq_w/2, "%c", label);
    }
    wattroff(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
}

/* draw_board
 * Scales square size to fill available space, then centers.
 * Keeps aspect: sq_w = sq_h * 2 + 1  (so pieces look square).
 */
/* Largest square whose whole board fits. sq_w = sq_h * 2 + 1 reads as
 * square because cells are about twice as tall as wide. Returns 0 if
 * even a 1-row square does not fit. */
static int fit_board(int avail_h, int avail_w, int framed,
                     int *out_h, int *out_w)
{
    int chrome_h = framed ? 3 : 1;   /* frame top+bottom + file labels */
    int chrome_w = framed ? 4 : 2;   /* rank label + frame either side */

    int best = 0;
    for (int h = 1; h <= 8; h++) {
        int w = h * 2 + 1;
        if (w < GLYPH_W + 2) w = GLYPH_W + 2;
        if (8 * h + chrome_h <= avail_h && 8 * w + chrome_w <= avail_w)
            best = h;
    }
    if (!best) return 0;

    *out_h = best;
    *out_w = best * 2 + 1;
    if (*out_w < GLYPH_W + 2) *out_w = GLYPH_W + 2;
    return 1;
}

static void draw_board(WINDOW *win, const TUIState *state)
{
    int wh, ww;
    getmaxyx(win, wh, ww);

    /* Available area (inside border, above status section) */
    /* Row 0 is the border and row 1 the clocks, so the board starts at 2. */
    int avail_h = wh - 8;   /* border + clock row + 5 status rows + border */
    int avail_w = ww - 6;   /* 2 borders + rank-label col + margins        */

    /* A frame costs 2 rows and 2 columns, so it is dropped rather than
     * allowed to push the bottom ranks off the window. */
    int sq_h, sq_w;
    int framed = 1;
    if (!fit_board(avail_h, avail_w, 1, &sq_h, &sq_w)) {
        framed = 0;
        if (!fit_board(avail_h, avail_w, 0, &sq_h, &sq_w)) {
            /* Nothing fits; draw the smallest board and let it clip. */
            sq_h = 1;
            sq_w = GLYPH_W + 2;
        }
    }

    int chrome_h = framed ? 3 : 1;
    int chrome_w = framed ? 4 : 2;
    int board_h = 8 * sq_h + chrome_h;
    int board_w = 8 * sq_w + chrome_w;

    /* Minimums keep the frame and rank labels inside the border. */
    int sr = 2 + (avail_h - board_h) / 2;
    int sc = 2 + (avail_w - board_w) / 2 + 2;
    int min_r = framed ? 3 : 2;
    int min_c = framed ? 4 : 3;
    if (sr < min_r) sr = min_r;
    if (sc < min_c) sc = min_c;

    draw_board_grid(win, state, sr, sc, sq_h, sq_w, framed);
}

/* Status bar (inside board window)  */
static void draw_status(WINDOW *win, const TUIState *state)
{
    int wh, ww;
    getmaxyx(win, wh, ww);

    /* Separator */
    wattron(win, COLOR_PAIR(CP_BORDER));
    mvwaddch(win, wh-6, 0, ACS_LTEE);
    hfill(win, wh-6, 1, ww-2, ACS_HLINE);
    mvwaddch(win, wh-6, ww-1, ACS_RTEE);
    wattroff(win, COLOR_PAIR(CP_BORDER));

    /* Status message */
    int is_err = strncmp(state->status,"Illegal",7)==0 ||
                 strncmp(state->status,"Bad",    3)==0 ||
                 strncmp(state->status,"Unknown",7)==0;
    attr_t st = is_err ? COLOR_PAIR(CP_STATUS_ERR)|A_BOLD
                       : COLOR_PAIR(CP_STATUS_OK) |A_BOLD;
    wattron(win, st);
    mvwprintw(win, wh-5, 2, "%-*.*s", ww-4, ww-4, state->status);
    wattroff(win, st);

    /* Right-anchored overlays are skipped on a narrow window, where they
     * would land on the text they are meant to sit beside. */
    if (ww >= 34 && is_in_check(&state->game.pos, state->game.pos.side)) {
        wattron(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
        mvw_clip(win, wh-5, ww-14, " !! CHECK !! ");
        wattroff(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
    }

    /* Hint line */
    wattron(win, COLOR_PAIR(CP_HINT));
    mvw_clip(win, wh-4, 2, "move:e2e4  go  u=undo  new  flip  depth N  quit  [Tab]=stats");
    wattroff(win, COLOR_PAIR(CP_HINT));

    /* Cursor position hint */
    {
        int cr = 7 - state->cursor_row;   /* rank number */
        int cf = state->cursor_col;        /* file index  */
        /* The hint beside it is a fixed 52 columns. */
        if (ww - 10 > 56) {
            wattron(win, COLOR_PAIR(CP_HINT));
            mvw_clip(win, wh-4, ww-10, "[%c%d]", 'a'+cf, cr+1);
            wattroff(win, COLOR_PAIR(CP_HINT));
        }
    }

    /* Game info */
    const char *side = state->game.pos.side == WHITE ? "White" : "Black";
    wattron(win, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);
    mvw_clip(win, wh-3, 2, "[ %s to move ]  depth:%d  eval:%s",
             side, state->engine_depth, state->last_eval);
    wattroff(win, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);

    /* Selection hint */
    if (state->selected && ww - 16 > 40) {
        int sr = 7 - state->sel_row;
        int sf = state->sel_col;
        wattron(win, COLOR_PAIR(CP_SEL_PC)|A_BOLD);
        mvw_clip(win, wh-3, ww-16, " selected:%c%d ", 'a'+sf, sr+1);
        wattroff(win, COLOR_PAIR(CP_SEL_PC)|A_BOLD);
    }

    /* Navigation hint */
    wattron(win, COLOR_PAIR(CP_HINT));
    mvw_clip(win, wh-2, 2, "arrows/hjkl=cursor   enter=select/move   esc=deselect");
    wattroff(win, COLOR_PAIR(CP_HINT));
}

/* Info panel   */
static void draw_captured_row(WINDOW *win, int row, int col,
                               const int cap[6], int white_sym, int maxcol)
{
    attr_t attr = white_sym ? (COLOR_PAIR(CP_CAP_W)|A_BOLD)
                            : (COLOR_PAIR(CP_CAP_B)|A_BOLD);
    int x = col, any = 0;
    for (int i = 0; i < 6 && x < maxcol-2; i++) {
        for (int j = 0; j < cap[i] && x < maxcol-2; j++) {
            int pidx = white_sym ? i : i + 6;
            cchar_t cc;
            wchar_t ws[2] = { PIECE_GLYPH[pidx], L'\0' };
            setcchar(&cc, ws, A_BOLD, (short)PAIR_NUMBER(attr), NULL);
            wattron(win, attr);
            mvwadd_wch(win, row, x, &cc);
            wattroff(win, attr);
            x += GLYPH_W; any = 1;
        }
    }
    if (!any) {
        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, row, col, "none");
        wattroff(win, COLOR_PAIR(CP_HINT));
    }
}

static void draw_info(WINDOW *win, const TUIState *state)
{
    wclear(win);
    int wh, ww;
    getmaxyx(win, wh, ww);

    /* Border */
    wattron(win, COLOR_PAIR(CP_BORDER));
    box(win, ACS_VLINE, ACS_HLINE);
    wattroff(win, COLOR_PAIR(CP_BORDER));

    /* Title in border */
    wattron(win, COLOR_PAIR(CP_TITLE)|A_BOLD);
    mvw_clip(win, 0, (ww-6)/2, " INFO ");
    wattroff(win, COLOR_PAIR(CP_TITLE)|A_BOLD);

    int row = 2;

    /* GAME section */
    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvw_clip(win, row++, 2, "GAME");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    const char *eng = state->engine_side == WHITE ? "White" :
                      state->engine_side == BLACK ? "Black" : "None";
    wattron(win, COLOR_PAIR(CP_INFO_VAL));
    mvw_clip(win, row++, 2, "depth : %d",  state->engine_depth);
    mvw_clip(win, row++, 2, "eval  : %s",  state->last_eval);
    mvw_clip(win, row++, 2, "side  : %s",  state->game.pos.side == WHITE ? "White" : "Black");
    mvw_clip(win, row++, 2, "engine: %s",  eng);
    wattroff(win, COLOR_PAIR(CP_INFO_VAL));

    if (is_in_check(&state->game.pos, state->game.pos.side)) {
        wattron(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
        mvw_clip(win, row++, 2, "** CHECK **");
        wattroff(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
    } else {
        row++;
    }

    /* MOVES section */
    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvw_clip(win, row++, 2, "MOVES");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    /* The two-column table needs ~30 columns, but the panel is 20 or 26
     * wide until the terminal reaches 90. Below that, a compact form. */
    int wide = (ww >= 30);

    if (wide) {
        wattron(win, COLOR_PAIR(CP_HINT));
        mvw_clip(win, row++, 1, "  # pc  mv   t  pc  mv   t");
        wattroff(win, COLOR_PAIR(CP_HINT));
    }

    /* CAPTURED is drawn from a fixed offset off the bottom while the move
     * list grows down, so the split has to come from real space. */
    /* 7 drawn rows; the 8th keeps the last off the bottom border. */
    const int CAP_ROWS_FULL = 11;
    const int CAP_ROWS_MIN  = 8;

    int cap_h = CAP_ROWS_FULL;
    if (wh - row - cap_h < 1)
        cap_h = wh - row - 1;       /* keep at least one move row */
    if (cap_h < CAP_ROWS_MIN)
        cap_h = 0;                  /* no room at all: drop the section */

    int hist_rows = (cap_h ? (wh - cap_h) : (wh - 1)) - row;
    if (hist_rows < 1) hist_rows = 1;

    /* First row the move list must not touch: the captured section's
     * separator if there is one, otherwise the panel's bottom border. */
    int move_limit = cap_h ? (wh - cap_h) : (wh - 1);

    int total  = (state->game.move_count + 1) / 2;
    int start  = total - hist_rows;
    if (start < 0) start = 0;

    for (int p = start; p < total && row < move_limit; p++) {
        int wi = p*2, bi = p*2+1;
        int latest = (p == total - 1);

        if (!wide) {
            /* "12. e2e4 e7e5" */
            attr_t a = latest ? COLOR_PAIR(CP_STATUS_OK)|A_BOLD
                              : COLOR_PAIR(CP_INFO_VAL);
            wattron(win, a);
            mvw_clip(win, row, 1, "%3d. %-5s %-5s", p+1,
                     state->game.move_history[wi],
                     bi < state->game.move_count ? state->game.move_history[bi] : "");
            wattroff(win, a);
            row++;
            continue;
        }

        /* Move number */
        wattron(win, COLOR_PAIR(CP_HINT));
        mvw_clip(win, row, 1, "%3d.", p+1);
        wattroff(win, COLOR_PAIR(CP_HINT));

        /* White move — glyph@6 move@8 time@14 */
        if (wi < state->game.move_count) {
            int pidx = state->game.move_piece[wi];
            attr_t wa = latest ? COLOR_PAIR(CP_STATUS_OK)|A_BOLD : COLOR_PAIR(CP_MOVE_W)|A_BOLD;
            if (pidx >= 0) {
                cchar_t cc;
                wchar_t ws2[2] = { PIECE_GLYPH[pidx], L'\0' };
                setcchar(&cc, ws2, A_BOLD, (short)PAIR_NUMBER(wa), NULL);
                wattron(win, wa);
                mvwadd_wch(win, row, 6, &cc);
                wattroff(win, wa);
            }
            wattron(win, wa);
            mvw_clip(win, row, 8, "%-5s", state->game.move_history[wi]);
            wattroff(win, wa);
            int t = state->game.move_time[wi];
            wattron(win, COLOR_PAIR(CP_HINT));
            if (t < 60)   mvw_clip(win, row, 14, "%2ds", t);
            else          mvw_clip(win, row, 14, "%dm%d", t/60, t%60);
            wattroff(win, COLOR_PAIR(CP_HINT));
        }

        /* Black move — glyph@19 move@21 time@27 */
        if (bi < state->game.move_count) {
            int pidx = state->game.move_piece[bi];
            attr_t ba = latest ? COLOR_PAIR(CP_STATUS_OK) : COLOR_PAIR(CP_MOVE_B);
            if (pidx >= 0) {
                cchar_t cc;
                wchar_t ws2[2] = { PIECE_GLYPH[pidx], L'\0' };
                setcchar(&cc, ws2, 0, (short)PAIR_NUMBER(ba), NULL);
                wattron(win, ba);
                mvwadd_wch(win, row, 19, &cc);
                wattroff(win, ba);
            }
            wattron(win, ba);
            mvw_clip(win, row, 21, "%-5s", state->game.move_history[bi]);
            wattroff(win, ba);
            int t = state->game.move_time[bi];
            wattron(win, COLOR_PAIR(CP_HINT));
            if (t < 60)   mvw_clip(win, row, 27, "%2ds", t);
            else          mvw_clip(win, row, 27, "%dm%d", t/60, t%60);
            wattroff(win, COLOR_PAIR(CP_HINT));
        }
        row++;
    }
    if (state->game.move_count == 0 && row < move_limit) {
        wattron(win, COLOR_PAIR(CP_HINT));
        mvw_clip(win, row, 2, "(no moves)");
        wattroff(win, COLOR_PAIR(CP_HINT));
    }

    /* Skipped entirely when cap_h came out 0. */
    if (cap_h == 0) { wnoutrefresh(win); return; }

    int ct = wh - cap_h;
    wattron(win, COLOR_PAIR(CP_BORDER));
    mvwaddch(win, ct, 0, ACS_LTEE);
    hfill(win, ct, 1, ww-2, ACS_HLINE);
    mvwaddch(win, ct, ww-1, ACS_RTEE);
    wattroff(win, COLOR_PAIR(CP_BORDER));
    ct++;

    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvw_clip(win, ct++, 2, "CAPTURED");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    int cw[6], cb[6];
    captured_counts(&state->game.pos, cw, cb);
    int adv = 0;
    for (int i = 0; i < 5; i++) adv += (cb[i] - cw[i]) * PC_VAL[i];

    wattron(win, COLOR_PAIR(CP_HINT)); mvw_clip(win, ct++, 2, "W took:"); wattroff(win, COLOR_PAIR(CP_HINT));
    draw_captured_row(win, ct++, 2, cb, 1, ww);
    wattron(win, COLOR_PAIR(CP_HINT)); mvw_clip(win, ct++, 2, "B took:"); wattroff(win, COLOR_PAIR(CP_HINT));
    draw_captured_row(win, ct++, 2, cw, 0, ww);

    attr_t aa = adv != 0 ? COLOR_PAIR(CP_STATUS_OK)|A_BOLD : COLOR_PAIR(CP_INFO_VAL);
    wattron(win, aa);
    if      (adv > 0) mvw_clip(win, ct++, 2, "+%d White", adv);
    else if (adv < 0) mvw_clip(win, ct++, 2, "+%d Black", -adv);
    else              mvw_clip(win, ct++, 2, "Even");
    wattroff(win, aa);

    wnoutrefresh(win);
}

/* Command bar  */
static void draw_cmd(WINDOW *win)
{
    wclear(win);
    wattron(win, COLOR_PAIR(CP_BORDER));
    box(win, ACS_VLINE, ACS_HLINE);
    wattroff(win, COLOR_PAIR(CP_BORDER));
    wattron(win, COLOR_PAIR(CP_CMD)|A_BOLD);
    mvwprintw(win, 1, 2, "command: ");
    wattroff(win, COLOR_PAIR(CP_CMD)|A_BOLD);
    wnoutrefresh(win);
}

/* draw_eval_bar
 * Vertical evaluation bar styled like chess.com:
 *   • Black fills from the top
 *   • White fills from the bottom
 *   • The boundary between them shifts based on centipawn eval
 *   • A small score label is shown at the boundary
 *   • "B" label at top, "W" label at bottom
 */
/* "W: 03:07.42". Minutes are clamped so the field can never outgrow the
 * buffer mid-render. */
#define CLOCK_MAX_MINUTES 99
#define CLOCK_STR_SIZE    16   /* "W: 99:59.99" + NUL, with room to spare */

static void format_clock(char side, long centiseconds, char *out)
{
    if (centiseconds < 0) centiseconds = 0;

    long minutes = centiseconds / 6000;
    long seconds = (centiseconds % 6000) / 100;
    long cs      = centiseconds % 100;

    if (minutes > CLOCK_MAX_MINUTES) {
        minutes = CLOCK_MAX_MINUTES;
        seconds = 59;
        cs      = 99;
    }

    snprintf(out, CLOCK_STR_SIZE, "%c: %02ld:%02ld.%02ld",
             side, minutes, seconds, cs);
}

static void draw_eval_bar(WINDOW *win, const TUIState *state)
{
    if (!win) return;
    wclear(win);

    int wh, ww;
    getmaxyx(win, wh, ww);

    /* Parse eval — already in pawns (e.g. "+1.50") from white's perspective */
    float pawns = 0.0f;
    sscanf(state->last_eval, "%f", &pawns);

    /* Clamp to ±10 for display */
    if (pawns >  10.0f) pawns =  10.0f;
    if (pawns < -10.0f) pawns = -10.0f;

    /* Bar occupies wh-4 rows (leave 1 top + 1 bottom for labels) */
    int bar_top    = 1;
    int bar_bottom = wh - 2;
    int bar_h      = bar_bottom - bar_top + 1;
    if (bar_h < 2) bar_h = 2;

    /* Boundary row: 0.0 → exact middle; +10 → top; -10 → bottom */
    float norm      = (10.0f - pawns) / 20.0f;   /* 0.0=all-white .. 1.0=all-black */
    int   split_row = bar_top + (int)(norm * (bar_h - 1) + 0.5f);
    if (split_row < bar_top)    split_row = bar_top;
    if (split_row > bar_bottom) split_row = bar_bottom;

    /* Draw bar cells — full width of the narrow window */
    for (int r = bar_top; r <= bar_bottom; r++) {
        int is_black = (r < split_row);
        attr_t a = is_black ? (COLOR_PAIR(CP_B_DARK) | A_BOLD)
                            : (COLOR_PAIR(CP_W_LIGHT) | A_BOLD);
        wattron(win, a);
        for (int c = 0; c < ww; c++)
            mvwaddch(win, r, c, ACS_BLOCK);
        wattroff(win, a);
    }

    /* Score label at the boundary (centred, on the split row) */
    {
        char label[12];
        if (pawns >= 0)
            snprintf(label, sizeof(label), "+%.1f", pawns);
        else
            snprintf(label, sizeof(label), "%.1f",  pawns);
        int llen = (int)strlen(label);
        int lc   = (ww - llen) / 2;
        if (lc < 0) lc = 0;

        /* Print score — white text if on black zone, black text if on white zone */
        attr_t la = (split_row > bar_top + bar_h / 2)
                    ? (COLOR_PAIR(CP_W_LIGHT) | A_BOLD)
                    : (COLOR_PAIR(CP_B_DARK)  | A_BOLD);
        wattron(win, la);
        mvwprintw(win, split_row, lc, "%.*s", ww, label);
        wattroff(win, la);
    }

    /* "B" at top, "W" at bottom */
    wattron(win, COLOR_PAIR(CP_HINT) | A_BOLD);
    mvwprintw(win, 0,      (ww - 1) / 2, "B");
    mvwprintw(win, wh - 1, (ww - 1) / 2, "W");
    wattroff(win, COLOR_PAIR(CP_HINT) | A_BOLD);

    wnoutrefresh(win);
}

/* render_all  */
void render_all(WINDOW *board_win, WINDOW *info_win, WINDOW *eval_bar_win,
                WINDOW *cmd_win,   const TUIState *state)
{
    wclear(board_win);
    int bh, bw;
    getmaxyx(board_win, bh, bw);
    (void)bh;

    /* Border */
    wattron(board_win, COLOR_PAIR(CP_BORDER));
    box(board_win, ACS_VLINE, ACS_HLINE);
    wattroff(board_win, COLOR_PAIR(CP_BORDER));

    /* Title */
    wattron(board_win, COLOR_PAIR(CP_TITLE)|A_BOLD);
    mvwprintw(board_win, 0, 2, " Dchess ");
    wattroff(board_win, COLOR_PAIR(CP_TITLE)|A_BOLD);

    /* Link (right-aligned) */
    wattron(board_win, COLOR_PAIR(CP_LINK)|A_BOLD);
    const char *brand = " github.com/DDumbying ";
    int bc = bw - (int)strlen(brand) - 1;
    if (bc > 10) mvwprintw(board_win, 0, bc, "%s", brand);
    wattroff(board_win, COLOR_PAIR(CP_LINK)|A_BOLD);

    /* Clock bar (row 1, inside border) ──
     * White clock left, Black clock right, ticking with centiseconds. */
    {
        struct timespec mono_now;
        clock_gettime(CLOCK_MONOTONIC, &mono_now);

        /* Elapsed centiseconds since turn started */
        long cs_elapsed = (mono_now.tv_sec  - state->game.turn_start_mono.tv_sec)  * 100
                        + (mono_now.tv_nsec - state->game.turn_start_mono.tv_nsec) / 10000000;
        if (cs_elapsed < 0) cs_elapsed = 0;

        /* Accumulated seconds converted to centiseconds */
        long ws_cs = (long)state->game.white_clock * 100;
        long bs_cs = (long)state->game.black_clock * 100;

        /* Add live ticking to whichever side is on move */
        if (!state->game.game_over && state->game.clock_started) {
            if (state->game.clock_side == WHITE) ws_cs += cs_elapsed;
            else                            bs_cs += cs_elapsed;
        }

        attr_t wa = (state->game.clock_side == WHITE && !state->game.game_over)
                    ? (COLOR_PAIR(CP_INFO_VAL)|A_BOLD)
                    : COLOR_PAIR(CP_HINT);
        attr_t ba = (state->game.clock_side == BLACK && !state->game.game_over)
                    ? (COLOR_PAIR(CP_INFO_VAL)|A_BOLD)
                    : COLOR_PAIR(CP_HINT);

        char wstr[CLOCK_STR_SIZE], bstr[CLOCK_STR_SIZE];
        format_clock('W', ws_cs, wstr);
        format_clock('B', bs_cs, bstr);

        wattron(board_win, wa);
        mvwprintw(board_win, 1, 2, "%s", wstr);
        wattroff(board_win, wa);

        wattron(board_win, ba);
        mvwprintw(board_win, 1, bw - (int)strlen(bstr) - 2, "%s", bstr);
        wattroff(board_win, ba);
    }

    draw_board(board_win, state);
    draw_status(board_win, state);
    wnoutrefresh(board_win);

    if (info_win)     draw_info(info_win, state);
    if (eval_bar_win) draw_eval_bar(eval_bar_win, state);
    draw_cmd(cmd_win);
}
