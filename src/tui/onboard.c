#include "tui/onboard.h"
#include "tui/render.h"
#include "tui/stats_tui.h"
#include "engine/board.h"
#include "engine/fen.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include "utils/theme.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>

/* Same color-pair IDs render.c/tui.c use -- there's no shared header
 * for these in this codebase, so each file that needs a few of them
 * redefines those (matches the existing convention in tui.c). */
#define CP_BORDER     21
#define CP_TITLE      22
#define CP_HINT       29
#define CP_STATUS_OK  27

enum { ROW_SIDE, ROW_DIFFICULTY, ROW_POSITION, ROW_THEME, ROW_START, ROW_COUNT };

/* choice.side: WHITE, BLACK, or SIDE_TWO_PLAYER (not a real "side",
 * just a third menu option alongside the two real ones). */
#define SIDE_TWO_PLAYER 2

typedef struct {
    int  side;
    int  difficulty;
    int  use_custom_fen;
    char fen[128];
    int  theme;
} OnboardChoice;

static const char *side_label(int side)
{
    return side == WHITE ? "White" : side == BLACK ? "Black" : "Two-Player";
}

static const char *diff_label(int d)
{
    return d == DIFF_EASY ? "Easy" : d == DIFF_HARD ? "Hard" : "Medium";
}

/* Simple inline text prompt on the given row of `win`. Returns 1 with
 * a validated FEN copied into `out` on Enter, or 0 on ESC / an empty /
 * an invalid FEN (caller decides what "0" should fall back to). */
static int prompt_fen(WINDOW *win, int row, int col, int width, char *out, size_t outsz)
{
    char buf[128] = {0};
    int len = 0;

    curs_set(1);
    while (1) {
        for (int c = col; c < col + width && c < getmaxx(win) - 1; c++)
            mvwaddch(win, row, c, ' ');
        mvwprintw(win, row, col, "%.*s", width - 1, buf);
        wmove(win, row, col + len);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 27) { curs_set(0); return 0; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) break;
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0) buf[--len] = '\0';
            continue;
        }
        if (ch >= 32 && ch < 127 &&
            len < (int)sizeof(buf) - 1 && len < width - 1) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }
    curs_set(0);

    if (len == 0) return 0;

    Position probe;
    if (!parse_fen(buf, &probe, NULL, NULL)) return 0;

    snprintf(out, outsz, "%s", buf);
    return 1;
}

int tui_onboarding(TUIState *state)
{
    OnboardChoice choice;
    choice.side           = state->two_player ? SIDE_TWO_PLAYER : state->player_side;
    choice.difficulty     = state->difficulty;
    choice.use_custom_fen = 0;
    choice.fen[0]         = '\0';
    choice.theme          = state->theme;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int pw = 62, ph = 17;
    if (pw > cols - 2) pw = cols - 2;
    if (ph > rows - 2) ph = rows - 2;
    int pr = (rows - ph) / 2;
    int pc = (cols - pw) / 2;
    if (pr < 0) pr = 0;
    if (pc < 0) pc = 0;

    WINDOW *win = newwin(ph, pw, pr, pc);
    keypad(win, TRUE);

    /* Every labeled row prints a fixed 16-char label (e.g. "Play as:        ")
     * followed by a value field. Both together must fit inside the box:
     * pw columns total, minus the border (2 cols) minus the left margin
     * (col 3 start, i.e. 2 cols in from the border) leaves (pw - 4)
     * interior columns to spend on "label + value" combined. */
    int label_w = 16;
    int content_w = (pw - 4) - label_w;
    if (content_w < 8) content_w = 8;
    int start_w = pw - 4; /* "> Start Game" has no separate label prefix */

    int cursor_row = ROW_SIDE;
    int result = 1; /* 1 = start game, 0 = quit */

    while (1) {
        werase(win);
        wattron(win, COLOR_PAIR(CP_BORDER));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(CP_BORDER));

        wattron(win, COLOR_PAIR(CP_TITLE) | A_BOLD);
        const char *title = " dchess -- New Game ";
        int title_col = (pw - (int)strlen(title)) / 2;
        if (title_col < 1) title_col = 1;
        mvwprintw(win, 0, title_col, "%s", title);
        wattroff(win, COLOR_PAIR(CP_TITLE) | A_BOLD);

        int diff_dim = (choice.side == SIDE_TWO_PLAYER);

        if (cursor_row == ROW_SIDE) wattron(win, A_REVERSE);
        mvwprintw(win, 2, 3, "Play as:        %-*.*s", content_w, content_w, side_label(choice.side));
        if (cursor_row == ROW_SIDE) wattroff(win, A_REVERSE);

        if (diff_dim) wattron(win, A_DIM);
        else if (cursor_row == ROW_DIFFICULTY) wattron(win, A_REVERSE);
        mvwprintw(win, 4, 3, "Difficulty:     %-*.*s", content_w, content_w,
                  diff_dim ? "n/a" : diff_label(choice.difficulty));
        if (diff_dim) wattroff(win, A_DIM);
        else if (cursor_row == ROW_DIFFICULTY) wattroff(win, A_REVERSE);

        if (cursor_row == ROW_POSITION) wattron(win, A_REVERSE);
        {
            const char *pos_label = choice.use_custom_fen
                ? (choice.fen[0] ? choice.fen : "<press Enter to type a FEN>")
                : "Standard";
            mvwprintw(win, 6, 3, "Position:       %-*.*s", content_w, content_w, pos_label);
        }
        if (cursor_row == ROW_POSITION) wattroff(win, A_REVERSE);

        if (cursor_row == ROW_THEME) wattron(win, A_REVERSE);
        mvwprintw(win, 8, 3, "Theme:          %-*.*s", content_w, content_w, theme_name(choice.theme));
        if (cursor_row == ROW_THEME) wattroff(win, A_REVERSE);

        wattron(win, COLOR_PAIR(CP_STATUS_OK) | A_BOLD);
        if (cursor_row == ROW_START) wattron(win, A_REVERSE);
        mvwprintw(win, 10, 3, "%-*.*s", start_w, start_w, "> Start Game");
        if (cursor_row == ROW_START) wattroff(win, A_REVERSE);
        wattroff(win, COLOR_PAIR(CP_STATUS_OK) | A_BOLD);

        /* Hint text, split across two lines and explicitly bounded to
         * the window's interior width so it can never overflow past
         * the border (that overflow used to corrupt the bottom edge
         * of this exact box). */
        int hint_w = pw - 4;
        if (hint_w < 1) hint_w = 1;
        wattron(win, COLOR_PAIR(CP_HINT));
        mvwprintw(win, ph - 3, 2, "%-.*s", hint_w, "up/down: move    left/right: change    s: view stats");
        mvwprintw(win, ph - 2, 2, "%-.*s", hint_w, "enter: select    esc: quit");
        wattroff(win, COLOR_PAIR(CP_HINT));

        wrefresh(win);

        int ch = wgetch(win);
        switch (ch) {
            case KEY_UP: case 'k':
                cursor_row = (cursor_row + ROW_COUNT - 1) % ROW_COUNT;
                if (cursor_row == ROW_DIFFICULTY && diff_dim)
                    cursor_row = (cursor_row + ROW_COUNT - 1) % ROW_COUNT;
                break;
            case KEY_DOWN: case 'j':
                cursor_row = (cursor_row + 1) % ROW_COUNT;
                if (cursor_row == ROW_DIFFICULTY && diff_dim)
                    cursor_row = (cursor_row + 1) % ROW_COUNT;
                break;
            case KEY_LEFT: case 'h':
                if (cursor_row == ROW_SIDE)
                    choice.side = (choice.side == WHITE) ? SIDE_TWO_PLAYER :
                                  (choice.side == SIDE_TWO_PLAYER ? BLACK : WHITE);
                else if (cursor_row == ROW_DIFFICULTY && !diff_dim)
                    choice.difficulty = (choice.difficulty + 3 - 1) % 3;
                else if (cursor_row == ROW_POSITION)
                    choice.use_custom_fen = !choice.use_custom_fen;
                else if (cursor_row == ROW_THEME) {
                    choice.theme = (choice.theme + theme_count() - 1) % theme_count();
                    init_colors(choice.theme); /* live preview */
                }
                break;
            case KEY_RIGHT: case 'l':
                if (cursor_row == ROW_SIDE)
                    choice.side = (choice.side == WHITE) ? BLACK :
                                  (choice.side == BLACK ? SIDE_TWO_PLAYER : WHITE);
                else if (cursor_row == ROW_DIFFICULTY && !diff_dim)
                    choice.difficulty = (choice.difficulty + 1) % 3;
                else if (cursor_row == ROW_POSITION)
                    choice.use_custom_fen = !choice.use_custom_fen;
                else if (cursor_row == ROW_THEME) {
                    choice.theme = (choice.theme + 1) % theme_count();
                    init_colors(choice.theme); /* live preview */
                }
                break;
            case 's': case 'S': {
                int srows, scols;
                getmaxyx(stdscr, srows, scols);
                WINDOW *sw = newwin(srows, scols, 0, 0);
                keypad(sw, TRUE);
                draw_stats_overlay(sw, &state->stats);
                doupdate();
                wgetch(sw);
                delwin(sw);
                /* draw_stats_overlay painted the whole screen; clear it
                 * back to blank so the popup redraws onto a clean
                 * background on the next loop iteration instead of
                 * leaving stats-screen leftovers around its edges. */
                werase(stdscr);
                refresh();
                break;
            }
            case '\n': case '\r': case KEY_ENTER:
                if (cursor_row == ROW_POSITION && choice.use_custom_fen) {
                    char entered[128];
                    if (prompt_fen(win, 6, 19, pw - 22, entered, sizeof(entered)))
                        snprintf(choice.fen, sizeof(choice.fen), "%s", entered);
                    else if (!choice.fen[0])
                        choice.use_custom_fen = 0; /* nothing valid entered yet: fall back */
                } else if (cursor_row == ROW_START) {
                    goto done;
                } else {
                    cursor_row = ROW_START;
                }
                break;
            case 27: /* ESC: quit dchess entirely */
                result = 0;
                goto done;
            default:
                break;
        }
    }

done:
    delwin(win);
    if (!result) return 0;

    CliArgs chosen;
    memset(&chosen, 0, sizeof(chosen));
    chosen.player_side  = (choice.side == BLACK) ? BLACK : WHITE;
    chosen.two_player   = (choice.side == SIDE_TWO_PLAYER);
    chosen.difficulty   = choice.difficulty;
    chosen.engine_depth = cli_depth_for_difficulty(choice.difficulty);
    chosen.theme        = choice.theme;
    if (choice.use_custom_fen && choice.fen[0])
        snprintf(chosen.fen, sizeof(chosen.fen), "%s", choice.fen);

    tui_init(state, &chosen);
    state->show_onboarding = 0; /* tui_init() re-derives this from `chosen`; force it off */
    return 1;
}
