#include "tui/tui.h"
#include "tui/render.h"
#include "tui/input.h"
#include "tui/commands.h"
#include "engine/board.h"
#include "utils/bitboard.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>

/* CP_CANVAS pair id — must match render.c */
#define CP_CANVAS 22

void tui_init(TUIState *state) {
    memset(state, 0, sizeof(*state));
    init_start_position(&state->pos);
    state->engine_depth = 5;
    state->engine_side  = BLACK;
    snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");
    snprintf(state->status,    sizeof(state->status),    "Ready. Make your move!");
}

void tui_cleanup(void) {
    endwin();
}

void tui_run(TUIState *state) {
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

    int info_w  = 24;
    if (cols < 60) info_w = 0;
    int board_w = cols - info_w;

    WINDOW *info_win  = (info_w > 0) ? newwin(main_h, info_w, 0, 0) : NULL;
    WINDOW *board_win = newwin(main_h, board_w, 0, info_w);
    WINDOW *cmd_win   = newwin(cmd_h,  cols,    main_h, 0);

    /* ── Canvas background ──────────────────────────────────────────
     * Only stdscr gets the canvas background — it shows through as the
     * "desktop" color behind and between all the sub-windows.
     * Sub-windows must NOT get wbkgd(canvas) because wclear() would
     * then flood them with the canvas color and make content invisible.
     * Sub-windows keep their default background (dark/transparent) so
     * their own color pairs render correctly on top.
     * ──────────────────────────────────────────────────────────────*/
    wbkgd(stdscr, COLOR_PAIR(CP_CANVAS));
    werase(stdscr);
    wrefresh(stdscr);

    keypad(board_win, TRUE);
    keypad(cmd_win,   TRUE);

    char cmd_buf[64];

    while (1) {
        /* Repaint stdscr canvas first so it's behind everything */
        werase(stdscr);
        wnoutrefresh(stdscr);

        render_all(board_win, info_win, cmd_win, state);

        /* touchwin fixes overlap/corruption: forces ncurses to repaint
         * every cell even if another terminal window dirtied the screen */
        touchwin(stdscr);
        touchwin(board_win);
        if (info_win) touchwin(info_win);
        touchwin(cmd_win);
        wnoutrefresh(stdscr);
        wnoutrefresh(board_win);
        if (info_win) wnoutrefresh(info_win);
        wnoutrefresh(cmd_win);
        doupdate();

        if (!read_command(cmd_win, cmd_buf, sizeof(cmd_buf)))
            continue;
        if (!cmd_buf[0]) continue;

        int ret = handle_command(state, cmd_buf);
        if (ret == -1) break;
    }

    delwin(board_win);
    if (info_win) delwin(info_win);
    delwin(cmd_win);
    tui_cleanup();
}
