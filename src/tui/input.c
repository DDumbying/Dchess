#include "tui/input.h"
#include "tui/render.h"
#include <string.h>
#include <ncurses.h>

/*
 * Read a line of input from the cmd window.
 * Redraws the prompt each time for cleanliness.
 * Returns 1 when Enter is pressed, 0 on ESC.
 */
int read_command(WINDOW *win, char *buf, int maxlen) {
    int h, w;
    getmaxyx(win, h, w);
    (void)h;

    /* Prompt position */
    const int prompt_col = 11;  /* len("| command: ") */
    int len = 0;
    buf[0]  = '\0';

    /* Show cursor, position it */
    curs_set(1);

    while (1) {
        /* Redraw input line cleanly every iteration */
        wattron(win,  COLOR_PAIR(7));
        mvwprintw(win, 1, 1, " command: ");
        wattroff(win, COLOR_PAIR(7));

        /* Clear text area */
        for (int c = prompt_col; c < w - 1; c++)
            mvwaddch(win, 1, c, ' ');

        /* Print current buffer */
        mvwprintw(win, 1, prompt_col, "%.*s", w - prompt_col - 2, buf);

        /* Place cursor */
        wmove(win, 1, prompt_col + len);
        wrefresh(win);

        int ch = wgetch(win);

        switch (ch) {
            case '\n':
            case '\r':
            case KEY_ENTER:
                curs_set(0);
                return 1;

            case 27:  /* ESC */
                buf[0] = '\0';
                len    = 0;
                curs_set(0);
                return 0;

            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (len > 0) {
                    buf[--len] = '\0';
                }
                break;

            case KEY_DC:  /* Delete key */
                /* ignore */
                break;

            default:
                if (ch >= 32 && ch < 127 && len < maxlen - 1) {
                    buf[len++] = (char)ch;
                    buf[len]   = '\0';
                }
                break;
        }
    }
}
