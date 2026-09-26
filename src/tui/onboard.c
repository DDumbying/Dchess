#include "tui/onboard.h"
#include "tui/colors.h"
#include "tui/panels.h"
#include "tui/panel.h"
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

enum { ROW_WHITE, ROW_BLACK, ROW_POSITION, ROW_THEME, ROW_START, ROW_COUNT };

/* Each player row cycles You, then the engine at each level. */
#define WHO_COUNT 4

typedef struct {
    int  who[2];          /* 0 = You, 1 + DIFF_* = the engine */
    int  use_custom_fen;
    char fen[128];
    int  theme;
} OnboardChoice;

static Player who_player(int who)
{
    return who == 0 ? player_human() : player_builtin(who - 1);
}

static int player_who(const Player *p)
{
    return p->kind == PLAYER_HUMAN ? 0 : p->level + 1;
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
    choice.who[WHITE] = player_who(&state->players[WHITE]);
    choice.who[BLACK] = player_who(&state->players[BLACK]);
    choice.use_custom_fen = 0;
    choice.fen[0]         = '\0';
    choice.theme          = state->theme;

    /* Geometry is recomputed every iteration rather than once up front,
     * and the window rebuilt whenever it changes. That is what makes this
     * screen survive a terminal resize: there is no KEY_RESIZE special
     * case, the panel simply notices it is the wrong size and replaces
     * itself. */
    int pw = 0, ph = 0, pr = 0, pc = 0;
    WINDOW *shadow = NULL, *win = NULL;

    int label_w = 16;
    int content_w = 8, start_w = 8;

    int cursor_row = ROW_WHITE;
    int result = 1; /* 1 = start game, 0 = quit */

    while (1) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        int want_w = 62, want_h = 17;
        if (want_w > cols - 2) want_w = cols - 2;
        if (want_h > rows - 2) want_h = rows - 2;
        if (want_w < 1) want_w = 1;
        if (want_h < 1) want_h = 1;
        int want_r = (rows - want_h) / 2;
        int want_c = (cols - want_w) / 2;
        if (want_r < 0) want_r = 0;
        if (want_c < 0) want_c = 0;

        if (!win || want_w != pw || want_h != ph ||
            want_r != pr || want_c != pc) {
            if (win) delwin(win);
            panel_shadow_destroy(shadow);

            pw = want_w; ph = want_h; pr = want_r; pc = want_c;

            werase(stdscr);
            wrefresh(stdscr);

            shadow = panel_shadow(ph, pw, pr, pc);
            win    = newwin(ph, pw, pr, pc);
            wbkgd(win, COLOR_PAIR(CP_CANVAS));
            keypad(win, TRUE);

            /* Every labeled row prints a fixed 16-char label (e.g.
             * "White:          ") followed by a value field. Both must
             * fit inside the box: pw columns total, minus the border
             * (2 cols) minus the left margin (col 3 start, i.e. 2 cols in
             * from the border) leaves (pw - 4) interior columns to spend
             * on "label + value" combined. */
            content_w = (pw - 4) - label_w;
            if (content_w < 8) content_w = 8;
            start_w = pw - 4;
            if (start_w < 8) start_w = 8;
        }

        werase(win);
        panel_frame(win, "dchess · new game", CP_ACC_BOARD);

        const char *row_name[2] = { "White:          ", "Black:          " };
        for (int side = WHITE; side <= BLACK; side++) {
            Player p = who_player(choice.who[side]);
            char label[32];
            player_label(&p, label, sizeof(label));
            int on = (cursor_row == (side == WHITE ? ROW_WHITE : ROW_BLACK));
            if (on) wattron(win, A_REVERSE);
            mvwprintw(win, 2 + side * 2, 3, "%s%-*.*s", row_name[side],
                      content_w, content_w, label);
            if (on) wattroff(win, A_REVERSE);
        }

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
                break;
            case KEY_DOWN: case 'j':
                cursor_row = (cursor_row + 1) % ROW_COUNT;
                break;
            case KEY_LEFT: case 'h':
                if (cursor_row == ROW_WHITE || cursor_row == ROW_BLACK) {
                    int s = (cursor_row == ROW_WHITE) ? WHITE : BLACK;
                    choice.who[s] = (choice.who[s] + WHO_COUNT - 1) % WHO_COUNT;
                }
                else if (cursor_row == ROW_POSITION)
                    choice.use_custom_fen = !choice.use_custom_fen;
                else if (cursor_row == ROW_THEME) {
                    choice.theme = (choice.theme + theme_count() - 1) % theme_count();
                    init_colors(choice.theme); /* live preview */
                }
                break;
            case KEY_RIGHT: case 'l':
                if (cursor_row == ROW_WHITE || cursor_row == ROW_BLACK) {
                    int s = (cursor_row == ROW_WHITE) ? WHITE : BLACK;
                    choice.who[s] = (choice.who[s] + 1) % WHO_COUNT;
                }
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
    panel_shadow_destroy(shadow);
    if (!result) return 0;

    CliArgs chosen;
    memset(&chosen, 0, sizeof(chosen));
    chosen.players[WHITE] = who_player(choice.who[WHITE]);
    chosen.players[BLACK] = who_player(choice.who[BLACK]);
    chosen.theme        = choice.theme;
    if (choice.use_custom_fen && choice.fen[0])
        snprintf(chosen.fen, sizeof(chosen.fen), "%s", choice.fen);

    tui_init(state, &chosen);
    state->show_onboarding = 0; /* tui_init() re-derives this from `chosen`; force it off */
    return 1;
}
