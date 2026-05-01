#include "tui/tui.h"
#include "tui/render.h"
#include "tui/input.h"
#include "tui/commands.h"
#include "engine/board.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>

#define CP_CANVAS 35   /* must match render.c */

void tui_init(TUIState *state) {
    memset(state, 0, sizeof(*state));
    init_start_position(&state->pos);
    state->engine_depth = 5;
    state->engine_side  = BLACK;
    state->cursor_row   = 6;  /* rank 2 — white pawn row */
    state->cursor_col   = 4;  /* e file */
    snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");
    snprintf(state->status,    sizeof(state->status),    "Ready. Use arrows to move cursor, Enter to select.");
}

void tui_cleanup(void) { endwin(); }

/* Build highlight[8][8] for legal moves from square (sel_row, sel_col) */
static void build_highlights(TUIState *state)
{
    memset(state->highlight, 0, sizeof(state->highlight));
    if (!state->selected) return;

    int rank = 7 - state->sel_row;
    int file = state->sel_col;
    int from = rank * 8 + file;

    MoveList ml;
    generate_moves(&state->pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from) continue;

        /* Validate — skip if leaves king in check */
        Position tmp;
        memcpy(&tmp, &state->pos, sizeof(Position));
        if (!make_move(&tmp, m)) continue;

        int to_sq   = TO(m);
        int to_rank = to_sq / 8;
        int to_file = to_sq % 8;
        int to_drow = 7 - to_rank;
        state->highlight[to_drow][to_file] = 1;
    }
}

/* Handle Enter on the cursor: select or move */
static void cursor_enter(TUIState *state)
{
    int rank = 7 - state->cursor_row;
    int file = state->cursor_col;
    int sq   = rank * 8 + file;

    if (!state->selected) {
        /* Select only if there's a friendly piece here */
        int piece = -1;
        for (int i = 0; i < 12; i++)
            if (GET_BIT(state->pos.bitboards[i], sq)) { piece = i; break; }

        int is_white_pc = (piece >= 0 && piece < 6);
        int is_black_pc = (piece >= 6);
        int friendly = (state->pos.side == WHITE && is_white_pc) ||
                       (state->pos.side == BLACK && is_black_pc);

        if (piece < 0 || !friendly) {
            snprintf(state->status, sizeof(state->status),
                     "No friendly piece on %c%d", 'a'+file, rank+1);
            return;
        }

        state->selected = 1;
        state->sel_row  = state->cursor_row;
        state->sel_col  = state->cursor_col;
        build_highlights(state);
        snprintf(state->status, sizeof(state->status),
                 "Selected %c%d — use arrows + Enter to move, Esc to cancel",
                 'a'+file, rank+1);
    } else {
        /* If pressing Enter on the selected square again → deselect */
        if (state->cursor_row == state->sel_row &&
            state->cursor_col == state->sel_col) {
            state->selected = 0;
            memset(state->highlight, 0, sizeof(state->highlight));
            snprintf(state->status, sizeof(state->status), "Deselected.");
            return;
        }

        /* Attempt move from sel to cursor */
        int from_rank = 7 - state->sel_row;
        int from_file = state->sel_col;
        int to_rank   = 7 - state->cursor_row;
        int to_file   = state->cursor_col;

        char mv[6];
        snprintf(mv, sizeof(mv), "%c%d%c%d",
                 'a'+from_file, from_rank+1,
                 'a'+to_file,   to_rank+1);

        /* try_move is in commands.c — replicate lightweight version here */
        int from_sq = from_rank * 8 + from_file;
        int to_sq   = to_rank   * 8 + to_file;

        MoveList ml;
        generate_moves(&state->pos, &ml);
        int moved = 0;
        for (int i = 0; i < ml.count; i++) {
            Move m = ml.moves[i];
            if (FROM(m) != from_sq || TO(m) != to_sq) continue;
            /* Default: prefer queen promo */
            if ((FLAGS(m) & FLAG_PROMOTION) && !(FLAGS(m) & FLAG_PROMO_Q))
                continue;

            Position saved;
            memcpy(&saved, &state->pos, sizeof(Position));
            if (!make_move(&state->pos, m)) {
                memcpy(&state->pos, &saved, sizeof(Position));
                snprintf(state->status, sizeof(state->status),
                         "Illegal: leaves king in check");
                break;
            }
            char buf[8];
            move_to_str(m, buf);
            if (state->move_count < MAX_MOVE_HISTORY)
                strncpy(state->move_history[state->move_count++], buf, 7);
            snprintf(state->status, sizeof(state->status), "Played: %s", buf);

            /* Auto-engine move */
            if (state->engine_side == state->pos.side && !state->game_over)
                handle_command(state, "go");

            moved = 1;
            break;
        }

        if (!moved && state->highlight[state->cursor_row][state->cursor_col] == 0) {
            /* Not a legal destination — re-select cursor's piece if friendly */
            snprintf(state->status, sizeof(state->status),
                     "Not a legal move. Select a highlighted square.");
        }

        state->selected = 0;
        memset(state->highlight, 0, sizeof(state->highlight));
    }
}

void tui_run(TUIState *state)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    init_colors();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int cmd_h  = 3;
    int main_h = rows - cmd_h;
    int info_w = (cols >= 60) ? 24 : 0;
    int board_w = cols - info_w;

    WINDOW *info_win  = info_w ? newwin(main_h, info_w,  0,      0) : NULL;
    WINDOW *board_win =          newwin(main_h, board_w, 0,  info_w);
    WINDOW *cmd_win   =          newwin(cmd_h,  cols,    main_h, 0);

    /* Canvas background on stdscr only (sub-windows keep their dark default) */
    wbkgd(stdscr, COLOR_PAIR(CP_CANVAS));
    werase(stdscr);
    wrefresh(stdscr);

    keypad(board_win, TRUE);
    keypad(cmd_win,   TRUE);

    char cmd_buf[64];

    while (1) {
        /* Render */
        werase(stdscr);
        wnoutrefresh(stdscr);
        render_all(board_win, info_win, cmd_win, state);
        touchwin(stdscr);
        touchwin(board_win);
        if (info_win) touchwin(info_win);
        touchwin(cmd_win);
        wnoutrefresh(stdscr);
        wnoutrefresh(board_win);
        if (info_win) wnoutrefresh(info_win);
        wnoutrefresh(cmd_win);
        doupdate();

        /* Input — try arrow keys first (non-blocking in cmd_win) */
        int ch = read_key(cmd_win, cmd_buf, sizeof(cmd_buf));

        switch (ch) {
            case KEY_UP:    case 'k':
                if (state->cursor_row > 0) state->cursor_row--;
                break;
            case KEY_DOWN:  case 'j':
                if (state->cursor_row < 7) state->cursor_row++;
                break;
            case KEY_LEFT:  case 'h':
                if (state->cursor_col > 0) state->cursor_col--;
                break;
            case KEY_RIGHT: case 'l':
                if (state->cursor_col < 7) state->cursor_col++;
                break;
            case '\n': case '\r': case KEY_ENTER:
                if (!state->game_over) cursor_enter(state);
                break;
            case 27: /* ESC — deselect */
                state->selected = 0;
                memset(state->highlight, 0, sizeof(state->highlight));
                snprintf(state->status, sizeof(state->status), "Deselected.");
                break;
            case -2: /* text command submitted */
                if (cmd_buf[0]) {
                    int ret = handle_command(state, cmd_buf);
                    if (ret == -1) goto quit;
                }
                break;
            default:
                break;
        }
    }
quit:
    delwin(board_win);
    if (info_win) delwin(info_win);
    delwin(cmd_win);
    tui_cleanup();
}
