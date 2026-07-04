#include "tui/input.h"
#include "tui/render.h"
#include <string.h>
#include <ncurses.h>

/*
 * read_key — unified input handler for the TUI.
 *
 * Vim-style modal input:
 *   Normal mode  (insert_mode == 0):
 *     * hjkl / arrow keys  -> cursor navigation (returned to caller)
 *     * 'i'                -> enter insert mode (returns 0)
 *     * Enter on empty buf -> cursor action (returned as '\n')
 *     * All other keys     -> ignored
 *
 *   Insert mode  (insert_mode == 1):
 *     * Printable chars    -> accumulated in ibuf
 *     * Enter              -> submit command (-2) or plain Enter ('\n')
 *     * Backspace          -> erase last char
 *     * ESC                -> clear buf, exit insert mode (returns 27)
 *
 * Returns:
 *   KEY_UP / KEY_DOWN / KEY_LEFT / KEY_RIGHT  -> arrow keys (normal mode)
 *   '\n'                                       -> Enter (cursor action)
 *   27                                         -> ESC (exits insert mode)
 *   -2                                         -> text command completed (in buf)
 *   0                                          -> absorbed keystroke
 */
int read_key(WINDOW *win, char *buf, int maxlen, int *insert_mode)
{
    static char ibuf[256];
    static int  ilen = 0;

    int h, w;
    getmaxyx(win, h, w);
    (void)h;

    const int prompt_col = 16;

    /* Redraw command bar */
    wattron(win, COLOR_PAIR(7));
    if (*insert_mode)
        mvwprintw(win, 1, 1, " -- INSERT --  ");
    else
        mvwprintw(win, 1, 1, " -- NORMAL --  ");
    wattroff(win, COLOR_PAIR(7));
    for (int c = prompt_col; c < w - 1; c++) mvwaddch(win, 1, c, ' ');
    if (*insert_mode)
        mvwprintw(win, 1, prompt_col, "%.*s", w - prompt_col - 2, ibuf);
    wmove(win, 1, prompt_col + (*insert_mode ? ilen : 0));
    curs_set(*insert_mode ? 1 : 0);
    wrefresh(win);

    int ch = wgetch(win);

    /* ESC always exits insert mode */
    if (ch == 27) {
        ibuf[0] = '\0';
        ilen = 0;
        *insert_mode = 0;
        curs_set(0);
        return 27;
    }

    /* Normal mode: navigation and mode-switch only */
    if (!*insert_mode) {
        switch (ch) {
            case KEY_UP:    case KEY_DOWN:
            case KEY_LEFT:  case KEY_RIGHT:
            case 'k': case 'j': case 'h': case 'l':
                return ch;
            case '\n': case '\r': case KEY_ENTER:
                return '\n';
            case '\t':   /* Tab — pass through for stats overlay */
                return '\t';
            case 'i':
                *insert_mode = 1;
                curs_set(1);
                return 0;
            default:
                return 0;
        }
    }

    /* Insert mode: accumulate command text */
    switch (ch) {
        case '\n': case '\r': case KEY_ENTER:
            if (ilen > 0) {
                strncpy(buf, ibuf, maxlen-1);
                buf[maxlen-1] = '\0';
                ibuf[0] = '\0';
                ilen = 0;
                *insert_mode = 0;
                curs_set(0);
                return -2;
            }
            /* Empty buffer Enter -> cursor action, exit insert mode */
            *insert_mode = 0;
            curs_set(0);
            return '\n';

        case KEY_BACKSPACE: case 127: case 8:
            if (ilen > 0) ibuf[--ilen] = '\0';
            break;

        default:
            if (ch >= 32 && ch < 127 && ilen < maxlen-1) {
                ibuf[ilen++] = (char)ch;
                ibuf[ilen]   = '\0';
            }
            break;
    }

    return 0;
}
