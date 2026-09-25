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
#include "tui/panels.h"
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

/* Direct-colour terminals (COLORS beyond 256, e.g. TERM=xterm-direct)
 * read a colour number as packed RGB, so palette index 235 would come out
 * as the blue 0x0000EB. Those get the index converted to its RGB value. */
static int direct_colour;

static void set_pair(int id, int fg, int bg)
{
    if (direct_colour)
        init_extended_pair(id, theme_rgb(fg), theme_rgb(bg));
    else
        init_pair(id, fg, bg);
}

static void init_palette_256(const Theme *t)
{
    int bg = t->bg;

    set_pair(CP_LIGHT,   t->sq_light, t->sq_light);
    set_pair(CP_DARK,    t->sq_dark,  t->sq_dark);
    set_pair(CP_W_LIGHT, t->pc_white, t->sq_light);
    set_pair(CP_W_DARK,  t->pc_white, t->sq_dark);
    set_pair(CP_B_LIGHT, t->pc_black, t->sq_light);
    set_pair(CP_B_DARK,  t->pc_black, t->sq_dark);

    set_pair(CP_CURSOR,       t->pc_black, t->cursor);
    set_pair(CP_CURSOR_PC,    t->pc_white, t->cursor);
    set_pair(CP_CURSOR_PC_B,  t->pc_black, t->cursor);
    set_pair(CP_SEL,          t->pc_black, t->sel);
    set_pair(CP_SEL_PC,       t->pc_white, t->sel);
    set_pair(CP_SEL_PC_B,     t->pc_black, t->sel);
    set_pair(CP_MOVE_HI,      t->pc_black, t->movehi);
    set_pair(CP_MOVE_HI_PC,   t->pc_white, t->movehi);
    set_pair(CP_MOVE_HI_PC_B, t->pc_black, t->movehi);
    set_pair(CP_CHECK_SQ,     t->pc_black, t->check);
    set_pair(CP_CHECK_PC,     t->pc_white, t->check);
    set_pair(CP_CHECK_PC_B,   t->pc_black, t->check);

    set_pair(CP_LMVL,   t->lm_light, t->lm_light);
    set_pair(CP_LMVD,   t->lm_dark,  t->lm_dark);
    set_pair(CP_W_LMVL, t->pc_white, t->lm_light);
    set_pair(CP_W_LMVD, t->pc_white, t->lm_dark);
    set_pair(CP_B_LMVL, t->pc_black, t->lm_light);
    set_pair(CP_B_LMVD, t->pc_black, t->lm_dark);

    set_pair(CP_BORDER,     t->line, bg);
    set_pair(CP_TITLE,      t->fg,   bg);
    set_pair(CP_LINK,       t->dim,  bg);
    set_pair(CP_LABEL,      t->dim,  bg);
    set_pair(CP_INFO_HEAD,  t->acc_board, bg);
    set_pair(CP_INFO_VAL,   t->fg,   bg);
    set_pair(CP_STATUS_OK,  t->ok,   bg);
    set_pair(CP_STATUS_ERR, t->err,  bg);
    set_pair(CP_HINT,       t->dim,  bg);
    set_pair(CP_CMD,        t->fg,   bg);
    set_pair(CP_MOVE_W,     t->fg,   bg);
    set_pair(CP_MOVE_B,     t->dim,  bg);
    set_pair(CP_CAP_W,      t->fg,   bg);
    set_pair(CP_CAP_B,      t->dim,  bg);
    set_pair(CP_CANVAS,     t->fg,   bg);
    set_pair(CP_SHADOW,     t->shadow, bg);
    set_pair(CP_FRAME,      t->line, bg);

    set_pair(CP_ACC_BOARD,  t->acc_board,  bg);
    set_pair(CP_ACC_EVAL,   t->acc_eval,   bg);
    set_pair(CP_ACC_CLOCK,  t->acc_clock,  bg);
    set_pair(CP_ACC_MOVES,  t->acc_moves,  bg);
    set_pair(CP_ACC_ENGINE, t->acc_engine, bg);
    set_pair(CP_TRACK,      t->track, t->track);
    for (int i = 0; i < THEME_RAMP; i++)
        set_pair(CP_RAMP_BASE + i, t->ramp[i], bg);
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
    direct_colour = COLORS > 256;
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
void mvw_clip(WINDOW *win, int row, int col, const char *fmt, ...)
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

static void draw_board_grid(WINDOW *win, const TUIState *state,
                            int start_row, int start_col,
                            int sq_h, int sq_w)
{
    const Position *pos = &state->game.pos;
    int flipped = (state->view_side == BLACK); /* Black at bottom when flipped */

    int w_chk = is_in_check(pos, WHITE);
    int b_chk = is_in_check(pos, BLACK);
    int wk_sq = pos->bitboards[K] ? lsb(pos->bitboards[K]) : -1;
    int bk_sq = pos->bitboards[k] ? lsb(pos->bitboards[k]) : -1;

    int lm_from, lm_to;
    parse_last_move(state, &lm_from, &lm_to);


    for (int rank = 7; rank >= 0; rank--) {
        /* When flipped, rank 0 (white's back rank) is at the top */
        int drow = flipped ? rank : (7 - rank);
        int base = start_row + drow * sq_h;

        /* Rank label */
        wattron(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
        mvwprintw(win, base + sq_h/2, start_col - 2, "%d", rank + 1);
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
    int lr = start_row + 8 * sq_h;
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
static int fit_board(int avail_h, int avail_w, int *out_h, int *out_w)
{
    const int chrome_h = 1;   /* file labels */
    const int chrome_w = 2;   /* rank labels */

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

    int avail_h = wh - 2;   /* inside the panel border */
    int avail_w = ww - 2;

    int sq_h, sq_w;
    if (!fit_board(avail_h, avail_w, &sq_h, &sq_w)) {
        sq_h = 1;
        sq_w = GLYPH_W + 2;
    }

    int board_h = 8 * sq_h + 1;   /* + file labels */
    int board_w = 8 * sq_w + 2;   /* + rank labels */
    int sr = 1 + (avail_h - board_h) / 2;
    int sc = 1 + (avail_w - board_w) / 2 + 2;
    if (sr < 1) sr = 1;
    if (sc < 3) sc = 3;

    draw_board_grid(win, state, sr, sc, sq_h, sq_w);
}

void render_all(WINDOW *board, WINDOW *side, WINDOW *cmd, const TUIState *state)
{
    werase(board);
    panel_frame(board, "dchess", CP_ACC_BOARD);

    int bh, bw;
    getmaxyx(board, bh, bw);
    const char *brand = " github.com/DDumbying ";
    if (bw > (int)strlen(brand) + 12) {
        wattron(board, COLOR_PAIR(CP_LINK));
        mvw_clip(board, 0, bw - (int)strlen(brand) - 2, "%s", brand);
        wattroff(board, COLOR_PAIR(CP_LINK));
    }
    if (is_in_check(&state->game.pos, state->game.pos.side)) {
        wattron(board, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        mvw_clip(board, bh - 1, bw - 10, " CHECK ");
        wattroff(board, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
    }

    draw_board(board, state);
    wnoutrefresh(board);

    draw_side_column(side, state);
    draw_command_bar(cmd, state);
}
