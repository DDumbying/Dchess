/* render.c — Dchess TUI rendering
 *
 * Rendering contract (per the spec):
 *   • Each square: SQ_W=5 cols × SQ_H=2 rows, fixed.
 *   • Row 0 of square: blank background fill
 *   • Row 1 of square: piece centered at col+2 (mid of 5)
 *   • Layers: (1) square bg  (2) move highlight  (3) cursor/selection  (4) piece
 *   • Colors strictly separated: square pair = bg only, piece pair = fg only
 *   • mvadd_wch() for all Unicode glyphs (correct wide-char width)
 */

#include "tui/render.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include <string.h>
#include <stdio.h>
#include <wchar.h>

/* ── Unicode pieces ────────────────────────────────────────────────────── */
static const wchar_t PIECE_GLYPH[12] = {
    0x2659, 0x2658, 0x2657, 0x2656, 0x2655, 0x2654,  /* ♙♘♗♖♕♔ white */
    0x265F, 0x265E, 0x265D, 0x265C, 0x265B, 0x265A   /* ♟♞♝♜♛♚ black */
};
#define GLYPH_W 2   /* every chess glyph occupies exactly 2 terminal cells */

/* ── Square dimensions — computed at render time to fill available space ── */
#define SQ_W_MIN 5
#define SQ_H_MIN 2

/* ── Color pair IDs ─────────────────────────────────────────────────────── */
#define CP_LIGHT        1   /* light square bg                    */
#define CP_DARK         2   /* dark  square bg                    */
#define CP_W_LIGHT      3   /* white piece fg on light sq         */
#define CP_W_DARK       4   /* white piece fg on dark  sq         */
#define CP_B_LIGHT      5   /* black piece fg on light sq         */
#define CP_B_DARK       6   /* black piece fg on dark  sq         */
#define CP_CURSOR       7   /* cursor highlight (no piece)        */
#define CP_CURSOR_PC    8   /* cursor highlight (piece)           */
#define CP_SEL          9   /* selected square bg                 */
#define CP_SEL_PC      10   /* selected square piece              */
#define CP_MOVE_HI     11   /* legal-move dest highlight bg       */
#define CP_MOVE_HI_PC  12   /* legal-move dest with piece         */
#define CP_CHECK_SQ    13   /* king-in-check square               */
#define CP_CHECK_PC    14   /* king piece on check sq             */
#define CP_LMVL        15   /* last-move light sq                 */
#define CP_LMVD        16   /* last-move dark  sq                 */
#define CP_W_LMVL      17
#define CP_W_LMVD      18
#define CP_B_LMVL      19
#define CP_B_LMVD      20
#define CP_BORDER      21
#define CP_TITLE       22
#define CP_LINK        23
#define CP_LABEL       24
#define CP_INFO_HEAD   25
#define CP_INFO_VAL    26
#define CP_STATUS_OK   27
#define CP_STATUS_ERR  28
#define CP_HINT        29
#define CP_CMD         30
#define CP_MOVE_W      31
#define CP_MOVE_B      32
#define CP_CAP_W       33
#define CP_CAP_B       34
#define CP_CANVAS      35

/* ── Custom color slot IDs (init_color) ──────────────────────────────────  */
#define COL_LIGHT    8
#define COL_DARK     9
#define COL_WPFG    10
#define COL_BPFG    11
#define COL_CURSOR  12
#define COL_SEL     13
#define COL_MOVEHI  14
#define COL_CHECK   15
#define COL_GOLD    16
#define COL_LMVL    17
#define COL_LMVD    18
#define COL_CANVAS  19
#define COL_CHROME  20

void init_colors(void)
{
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    if (can_change_color()) {
        /* ── Define palette ── */
        init_color(COL_LIGHT,  870, 820, 710);  /* warm parchment       */
        init_color(COL_DARK,   360, 250, 150);  /* rich walnut          */
        init_color(COL_WPFG,   970, 960, 920);  /* bright cream (white) */
        init_color(COL_BPFG,   150, 310, 760);  /* cobalt blue  (black) */
        init_color(COL_CURSOR,  80, 680, 680);  /* teal cursor          */
        init_color(COL_SEL,    150, 680, 150);  /* green selection      */
        init_color(COL_MOVEHI,  80, 120, 500);  /* blue move highlight  */
        init_color(COL_CHECK,  820, 130, 100);  /* vivid red            */
        init_color(COL_GOLD,   990, 880, 200);  /* bright gold          */
        init_color(COL_LMVL,   780, 830, 540);  /* last-move light      */
        init_color(COL_LMVD,   390, 510, 180);  /* last-move dark       */
        init_color(COL_CANVAS,  90, 110, 100);  /* dark charcoal canvas */
        init_color(COL_CHROME, 500, 680, 680);  /* steel teal           */

        /* ── Board squares (fg = bg = same, invisible on empty cells) ── */
        init_pair(CP_LIGHT,      COL_LIGHT,  COL_LIGHT);
        init_pair(CP_DARK,       COL_DARK,   COL_DARK);

        /* ── Normal piece pairs ── */
        init_pair(CP_W_LIGHT,    COL_WPFG,   COL_LIGHT);
        init_pair(CP_W_DARK,     COL_WPFG,   COL_DARK);
        init_pair(CP_B_LIGHT,    COL_BPFG,   COL_LIGHT);
        init_pair(CP_B_DARK,     COL_BPFG,   COL_DARK);

        /* ── Cursor (arrow-key highlight) ── */
        init_pair(CP_CURSOR,     COL_CANVAS, COL_CURSOR);
        init_pair(CP_CURSOR_PC,  COL_WPFG,   COL_CURSOR);

        /* ── Selection ── */
        init_pair(CP_SEL,        COL_CANVAS, COL_SEL);
        init_pair(CP_SEL_PC,     COL_WPFG,   COL_SEL);

        /* ── Legal move destination highlight ── */
        init_pair(CP_MOVE_HI,    COL_CANVAS, COL_MOVEHI);
        init_pair(CP_MOVE_HI_PC, COL_WPFG,   COL_MOVEHI);

        /* ── Check ── */
        init_pair(CP_CHECK_SQ,   COL_GOLD,   COL_CHECK);
        init_pair(CP_CHECK_PC,   COL_GOLD,   COL_CHECK);

        /* ── Last-move ── */
        init_pair(CP_LMVL,       COL_LIGHT,  COL_LMVL);
        init_pair(CP_LMVD,       COL_DARK,   COL_LMVD);
        init_pair(CP_W_LMVL,     COL_WPFG,   COL_LMVL);
        init_pair(CP_W_LMVD,     COL_WPFG,   COL_LMVD);
        init_pair(CP_B_LMVL,     COL_BPFG,   COL_LMVL);
        init_pair(CP_B_LMVD,     COL_BPFG,   COL_LMVD);

        /* ── UI chrome ── */
        init_pair(CP_BORDER,     COL_CHROME,  -1);
        init_pair(CP_TITLE,      COL_CHROME,  -1);
        init_pair(CP_LINK,       COLOR_WHITE, -1);
        init_pair(CP_LABEL,      COL_CHROME,  -1);
        init_pair(CP_INFO_HEAD,  COLOR_YELLOW,-1);
        init_pair(CP_INFO_VAL,   COLOR_WHITE, -1);
        init_pair(CP_STATUS_OK,  COLOR_GREEN, -1);
        init_pair(CP_STATUS_ERR, COLOR_RED,   -1);
        init_pair(CP_HINT,       COL_CHROME,  -1);
        init_pair(CP_CMD,        COLOR_WHITE, -1);
        init_pair(CP_MOVE_W,     COLOR_WHITE, -1);
        init_pair(CP_MOVE_B,     COL_CHROME,  -1);
        init_pair(CP_CAP_W,      COLOR_WHITE, -1);
        init_pair(CP_CAP_B,      COLOR_YELLOW,-1);
        init_pair(CP_CANVAS,     COL_CANVAS,  COL_CANVAS);

    } else {
        /* ── 8-color fallback ── */
        init_pair(CP_LIGHT,      COLOR_BLACK,  COLOR_WHITE);
        init_pair(CP_DARK,       COLOR_WHITE,  COLOR_BLACK);
        init_pair(CP_W_LIGHT,    COLOR_BLUE,   COLOR_WHITE);
        init_pair(CP_W_DARK,     COLOR_WHITE,  COLOR_BLACK);
        init_pair(CP_B_LIGHT,    COLOR_RED,    COLOR_WHITE);
        init_pair(CP_B_DARK,     COLOR_RED,    COLOR_BLACK);
        init_pair(CP_CURSOR,     COLOR_BLACK,  COLOR_CYAN);
        init_pair(CP_CURSOR_PC,  COLOR_WHITE,  COLOR_CYAN);
        init_pair(CP_SEL,        COLOR_BLACK,  COLOR_GREEN);
        init_pair(CP_SEL_PC,     COLOR_WHITE,  COLOR_GREEN);
        init_pair(CP_MOVE_HI,    COLOR_WHITE,  COLOR_BLUE);
        init_pair(CP_MOVE_HI_PC, COLOR_WHITE,  COLOR_BLUE);
        init_pair(CP_CHECK_SQ,   COLOR_YELLOW, COLOR_RED);
        init_pair(CP_CHECK_PC,   COLOR_YELLOW, COLOR_RED);
        init_pair(CP_LMVL,       COLOR_BLACK,  COLOR_GREEN);
        init_pair(CP_LMVD,       COLOR_WHITE,  COLOR_GREEN);
        init_pair(CP_W_LMVL,     COLOR_BLUE,   COLOR_GREEN);
        init_pair(CP_W_LMVD,     COLOR_WHITE,  COLOR_GREEN);
        init_pair(CP_B_LMVL,     COLOR_RED,    COLOR_GREEN);
        init_pair(CP_B_LMVD,     COLOR_RED,    COLOR_GREEN);
        init_pair(CP_BORDER,     COLOR_CYAN,   -1);
        init_pair(CP_TITLE,      COLOR_CYAN,   -1);
        init_pair(CP_LINK,       COLOR_WHITE,  -1);
        init_pair(CP_LABEL,      COLOR_CYAN,   -1);
        init_pair(CP_INFO_HEAD,  COLOR_YELLOW, -1);
        init_pair(CP_INFO_VAL,   COLOR_WHITE,  -1);
        init_pair(CP_STATUS_OK,  COLOR_GREEN,  -1);
        init_pair(CP_STATUS_ERR, COLOR_RED,    -1);
        init_pair(CP_HINT,       COLOR_CYAN,   -1);
        init_pair(CP_CMD,        COLOR_WHITE,  -1);
        init_pair(CP_MOVE_W,     COLOR_WHITE,  -1);
        init_pair(CP_MOVE_B,     COLOR_CYAN,   -1);
        init_pair(CP_CAP_W,      COLOR_BLUE,   -1);
        init_pair(CP_CAP_B,      COLOR_RED,    -1);
        init_pair(CP_CANVAS,     COLOR_WHITE,  COLOR_BLACK);
    }
}

/* ── Internal helpers ───────────────────────────────────────────────────── */
static int piece_at(const Position *pos, int sq)
{
    for (int i = 0; i < 12; i++)
        if (GET_BIT(pos->bitboards[i], sq)) return i;
    return -1;
}

static void hfill(WINDOW *w, int r, int c, int len, chtype ch)
{
    for (int i = 0; i < len; i++) mvwaddch(w, r, c + i, ch);
}

static void parse_last_move(const TUIState *s, int *from, int *to)
{
    *from = *to = -1;
    if (s->move_count < 1) return;
    const char *mv = s->move_history[s->move_count - 1];
    if (!mv || strlen(mv) < 4) return;
    if (mv[0]<'a'||mv[0]>'h'||mv[2]<'a'||mv[2]>'h') return;
    if (mv[1]<'1'||mv[1]>'8'||mv[3]<'1'||mv[3]>'8') return;
    *from = (mv[1]-'1')*8 + (mv[0]-'a');
    *to   = (mv[3]-'1')*8 + (mv[2]-'a');
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

/* Write one wide chess glyph using cchar_t (correct 2-cell width) */
static void put_glyph(WINDOW *win, int r, int c, int piece, attr_t attr)
{
    cchar_t cc;
    wchar_t ws[2] = { PIECE_GLYPH[piece], L'\0' };
    /* extract only the style bits — PAIR_NUMBER needs the pair portion */
    attr_t style = attr & (A_BOLD | A_DIM | A_UNDERLINE | A_REVERSE);
    setcchar(&cc, ws, style, (short)PAIR_NUMBER(attr), NULL);
    wattron(win, attr);
    mvwadd_wch(win, r, c, &cc);
    wattroff(win, attr);
}

/* ── draw_square ─────────────────────────────────────────────────────────
 *
 * Draws one SQ_W × SQ_H square at window coordinates (row, col).
 *
 * Layout (SQ_W=5, SQ_H=2):
 *   row+0:  "     "    ← sq_attr (background fill)
 *   row+1:  " ♙♙ "    ← sq_attr padding + pc_attr glyph (2 cells) + sq_attr padding
 *                         piece centered at col+1 (lpad=1, glyph=2, rpad=2)
 *
 * sq_attr : color pair for background cells  (fg == bg on empty squares)
 * pc_attr : color pair for the glyph         (proper fg on same bg)
 * piece   : 0-11 index, or -1 for empty
 * dot     : draw a subtle "•" indicator for legal-move destinations
 * ──────────────────────────────────────────────────────────────────────── */
static void draw_square(WINDOW *win,
                        int row, int col,
                        int sq_h, int sq_w,
                        int piece,
                        attr_t sq_attr, attr_t pc_attr,
                        int dot)
{
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

/* ── draw_board_grid ─────────────────────────────────────────────────────
 *
 * Renders all 64 squares with rank/file labels.
 * start_row, start_col = top-left of rank-8, file-a square (inside border).
 * Label column is 2 chars to the left of start_col.
 * ─────────────────────────────────────────────────────────────────────── */
static void draw_board_grid(WINDOW *win, const TUIState *state,
                            int start_row, int start_col,
                            int sq_h, int sq_w)
{
    const Position *pos = &state->pos;

    int w_chk = is_in_check(pos, WHITE);
    int b_chk = is_in_check(pos, BLACK);
    int wk_sq = pos->bitboards[K] ? lsb(pos->bitboards[K]) : -1;
    int bk_sq = pos->bitboards[k] ? lsb(pos->bitboards[k]) : -1;

    int lm_from, lm_to;
    parse_last_move(state, &lm_from, &lm_to);

    for (int rank = 7; rank >= 0; rank--) {
        int drow = 7 - rank;
        int base = start_row + drow * sq_h;

        /* Rank label on middle row of the rank band */
        wattron(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
        mvwprintw(win, base + sq_h/2, start_col - 2, "%d ", rank + 1);
        wattroff(win, COLOR_PAIR(CP_LABEL) | A_BOLD);

        for (int file = 0; file < 8; file++) {
            int sq    = rank * 8 + file;
            int piece = piece_at(pos, sq);
            int light = (rank + file) % 2 != 0;

            /* ── Priority: check > cursor > selected > move-hi > last-move > normal ── */
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

            /* ── Square background attr ── */
            attr_t sq_attr;
            if      (is_check)  sq_attr = COLOR_PAIR(CP_CHECK_SQ);
            else if (is_sel)    sq_attr = COLOR_PAIR(CP_SEL);
            else if (is_cursor) sq_attr = COLOR_PAIR(CP_CURSOR);
            else if (is_movehi) sq_attr = COLOR_PAIR(CP_MOVE_HI);
            else if (is_lmv)    sq_attr = COLOR_PAIR(light ? CP_LMVL : CP_LMVD);
            else                sq_attr = COLOR_PAIR(light ? CP_LIGHT : CP_DARK);

            /* ── Piece foreground attr ── */
            attr_t pc_attr = sq_attr; /* default: invisible (empty) */
            if (piece >= 0) {
                int iw = (piece < 6);
                if      (is_check)  pc_attr = COLOR_PAIR(CP_CHECK_PC)   | A_BOLD;
                else if (is_sel)    pc_attr = COLOR_PAIR(CP_SEL_PC)     | A_BOLD;
                else if (is_cursor) pc_attr = COLOR_PAIR(CP_CURSOR_PC)  | A_BOLD;
                else if (is_movehi) pc_attr = COLOR_PAIR(CP_MOVE_HI_PC) | A_BOLD;
                else if (is_lmv) {
                    if (iw) pc_attr = COLOR_PAIR(light ? CP_W_LMVL : CP_W_LMVD) | A_BOLD;
                    else    pc_attr = COLOR_PAIR(light ? CP_B_LMVL : CP_B_LMVD) | A_BOLD;
                } else {
                    if (iw) pc_attr = COLOR_PAIR(light ? CP_W_LIGHT : CP_W_DARK) | A_BOLD;
                    else    pc_attr = COLOR_PAIR(light ? CP_B_LIGHT : CP_B_DARK) | A_BOLD;
                }
            }

            int dot = (is_movehi && piece < 0); /* bullet on empty legal-move sq */
            int col = start_col + file * sq_w;
            draw_square(win, base, col, sq_h, sq_w, piece, sq_attr, pc_attr, dot);
        }
    }

    /* File labels (a-h) centred under each column */
    int lr = start_row + 8 * sq_h;
    wattron(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
    for (int f = 0; f < 8; f++)
        mvwprintw(win, lr, start_col + f * sq_w + sq_w/2, "%c", 'a' + f);
    wattroff(win, COLOR_PAIR(CP_LABEL) | A_BOLD);
}

/* ── draw_board ──────────────────────────────────────────────────────────
 *
 * Scales square size to fill available space, then centers.
 * Keeps aspect: sq_w = sq_h * 2 + 1  (so pieces look square).
 * ─────────────────────────────────────────────────────────────────────── */
static void draw_board(WINDOW *win, const TUIState *state)
{
    int wh, ww;
    getmaxyx(win, wh, ww);

    /* Available area (inside border, above status section) */
    int avail_h = wh - 7;   /* top border + 5 status rows + bottom border */
    int avail_w = ww - 6;   /* 2 borders + 2 rank-label cols + 2 margin   */

    /* Scale up from minimums to fill space.
     * Constraint: sq_w = sq_h * 2 + 1  keeps the board square-ish.
     * We compute the max sq_h that fits in both dimensions. */
    int sq_h = avail_h / 8;
    if (sq_h < SQ_H_MIN) sq_h = SQ_H_MIN;

    /* Derive sq_w from sq_h, then check horizontal fit */
    int sq_w = sq_h * 2 + 1;
    if (sq_w < SQ_W_MIN) sq_w = SQ_W_MIN;

    /* If too wide, shrink sq_w (and sq_h to match) */
    while (sq_w * 8 > avail_w && sq_w > SQ_W_MIN) {
        sq_w--;
        sq_h = (sq_w - 1) / 2;
        if (sq_h < SQ_H_MIN) { sq_h = SQ_H_MIN; break; }
    }

    /* Piece must always fit: sq_w >= GLYPH_W + 1 padding each side */
    if (sq_w < GLYPH_W + 2) sq_w = GLYPH_W + 2;

    int board_h = 8 * sq_h + 1;   /* +1 for file labels */
    int board_w = 2 + 8 * sq_w;   /* +2 for rank labels */

    /* Centre inside available area */
    int sr = 1 + (avail_h - board_h) / 2;
    int sc = 2 + (avail_w - board_w) / 2 + 2;   /* +2 rank-label offset */
    if (sr < 1) sr = 1;
    if (sc < 4) sc = 4;

    draw_board_grid(win, state, sr, sc, sq_h, sq_w);
}

/* ── Status bar (inside board window) ──────────────────────────────────── */
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

    /* Check callout */
    if (is_in_check(&state->pos, state->pos.side)) {
        wattron(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
        mvwprintw(win, wh-5, ww-14, " !! CHECK !! ");
        wattroff(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
    }

    /* Hint line */
    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, wh-4, 2, "move:e2e4  go  new  flip  depth N  quit");
    wattroff(win, COLOR_PAIR(CP_HINT));

    /* Cursor position hint */
    {
        int cr = 7 - state->cursor_row;   /* rank number */
        int cf = state->cursor_col;        /* file index  */
        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, wh-4, ww-10, "[%c%d]    ", 'a'+cf, cr+1);
        wattroff(win, COLOR_PAIR(CP_HINT));
    }

    /* Game info */
    const char *side = state->pos.side == WHITE ? "White" : "Black";
    wattron(win, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);
    mvwprintw(win, wh-3, 2, "[ %s to move ]  depth:%d  eval:%s",
              side, state->engine_depth, state->last_eval);
    wattroff(win, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);

    /* Selection hint */
    if (state->selected) {
        int sr = 7 - state->sel_row;
        int sf = state->sel_col;
        wattron(win, COLOR_PAIR(CP_SEL_PC)|A_BOLD);
        mvwprintw(win, wh-3, ww-16, " selected:%c%d  ", 'a'+sf, sr+1);
        wattroff(win, COLOR_PAIR(CP_SEL_PC)|A_BOLD);
    }

    /* Navigation hint */
    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, wh-2, 2, "arrows/hjkl=cursor   enter=select/move   esc=deselect");
    wattroff(win, COLOR_PAIR(CP_HINT));
}

/* ── Info panel ──────────────────────────────────────────────────────────  */
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
    mvwprintw(win, 0, (ww-6)/2, " INFO ");
    wattroff(win, COLOR_PAIR(CP_TITLE)|A_BOLD);

    int row = 2;

    /* GAME section */
    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvwprintw(win, row++, 2, "GAME");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    const char *eng = state->engine_side == WHITE ? "White" :
                      state->engine_side == BLACK ? "Black" : "None";
    wattron(win, COLOR_PAIR(CP_INFO_VAL));
    mvwprintw(win, row++, 2, "depth : %d",  state->engine_depth);
    mvwprintw(win, row++, 2, "eval  : %s",  state->last_eval);
    mvwprintw(win, row++, 2, "side  : %s",  state->pos.side == WHITE ? "White" : "Black");
    mvwprintw(win, row++, 2, "engine: %s",  eng);
    wattroff(win, COLOR_PAIR(CP_INFO_VAL));

    if (is_in_check(&state->pos, state->pos.side)) {
        wattron(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
        mvwprintw(win, row++, 2, "** CHECK **");
        wattroff(win, COLOR_PAIR(CP_STATUS_ERR)|A_BOLD);
    } else {
        row++;
    }

    /* MOVES section */
    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvwprintw(win, row++, 2, "MOVES");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, row++, 2, "  #  W    B");
    wattroff(win, COLOR_PAIR(CP_HINT));

    int cap_h     = 11;
    int hist_rows = wh - row - cap_h;
    if (hist_rows < 1) hist_rows = 1;

    int total  = (state->move_count + 1) / 2;
    int start  = total - hist_rows;
    if (start < 0) start = 0;

    for (int p = start; p < total && row < wh - cap_h; p++) {
        int wi = p*2, bi = p*2+1;
        char wm[9]="-", bm[9]="-";
        if (wi < state->move_count) strncpy(wm, state->move_history[wi], 8);
        if (bi < state->move_count) strncpy(bm, state->move_history[bi], 8);
        int latest = (p == total - 1);

        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, row, 2, "%3d.", p+1);
        wattroff(win, COLOR_PAIR(CP_HINT));

        attr_t wa = latest ? COLOR_PAIR(CP_STATUS_OK)|A_BOLD : COLOR_PAIR(CP_MOVE_W)|A_BOLD;
        attr_t ba = latest ? COLOR_PAIR(CP_STATUS_OK)        : COLOR_PAIR(CP_MOVE_B);
        wattron(win, wa); mvwprintw(win, row, 7,  "%-4s", wm); wattroff(win, wa);
        wattron(win, ba); mvwprintw(win, row, 12, "%-4s", bm); wattroff(win, ba);
        row++;
    }
    if (state->move_count == 0) {
        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, row, 2, "(no moves)");
        wattroff(win, COLOR_PAIR(CP_HINT));
    }

    /* CAPTURED section */
    int ct = wh - cap_h;
    wattron(win, COLOR_PAIR(CP_BORDER));
    mvwaddch(win, ct, 0, ACS_LTEE);
    hfill(win, ct, 1, ww-2, ACS_HLINE);
    mvwaddch(win, ct, ww-1, ACS_RTEE);
    wattroff(win, COLOR_PAIR(CP_BORDER));
    ct++;

    wattron(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);
    mvwprintw(win, ct++, 2, "CAPTURED");
    wattroff(win, COLOR_PAIR(CP_INFO_HEAD)|A_BOLD);

    int cw[6], cb[6];
    captured_counts(&state->pos, cw, cb);
    int adv = 0;
    for (int i = 0; i < 5; i++) adv += (cb[i] - cw[i]) * PC_VAL[i];

    wattron(win, COLOR_PAIR(CP_HINT)); mvwprintw(win, ct++, 2, "W took:"); wattroff(win, COLOR_PAIR(CP_HINT));
    draw_captured_row(win, ct++, 2, cb, 1, ww);
    wattron(win, COLOR_PAIR(CP_HINT)); mvwprintw(win, ct++, 2, "B took:"); wattroff(win, COLOR_PAIR(CP_HINT));
    draw_captured_row(win, ct++, 2, cw, 0, ww);

    attr_t aa = adv != 0 ? COLOR_PAIR(CP_STATUS_OK)|A_BOLD : COLOR_PAIR(CP_INFO_VAL);
    wattron(win, aa);
    if      (adv > 0) mvwprintw(win, ct++, 2, "+%d White", adv);
    else if (adv < 0) mvwprintw(win, ct++, 2, "+%d Black", -adv);
    else              mvwprintw(win, ct++, 2, "Even");
    wattroff(win, aa);

    wattron(win, COLOR_PAIR(CP_HINT));
    mvwprintw(win, wh-3, 2, "Dchess");
    mvwprintw(win, wh-2, 2, "DDumbying.Org");
    wattroff(win, COLOR_PAIR(CP_HINT));

    wnoutrefresh(win);
}

/* ── Command bar ────────────────────────────────────────────────────────── */
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

/* ── render_all ─────────────────────────────────────────────────────────── */
void render_all(WINDOW *board_win, WINDOW *info_win,
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

    draw_board(board_win, state);
    draw_status(board_win, state);
    wnoutrefresh(board_win);

    if (info_win) draw_info(info_win, state);
    draw_cmd(cmd_win);
}
