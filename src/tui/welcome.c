#include "tui/welcome.h"
#include "tui/art.h"
#include "tui/colors.h"
#include "tui/render.h"
#include "game/profiles.h"
#include "game/records.h"
#include "utils/stats.h"
#include "utils/text.h"
#include "utils/theme.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static void centre(int y, int cols, const char *s)
{
    int w = text_width(s);
    mvw_fit(stdscr, y, w < cols ? (cols - w) / 2 : 0, w < cols ? w : cols, s);
}

static void draw(const TUIState *s, const char *name, const char *msg, int msg_err)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();
    if (rows < 12 || cols < 40) {
        centre(rows / 2, cols, "terminal too small");
        refresh();
        return;
    }
    int big = rows >= 20 && cols >= 60;
    int y = (rows - (big ? 14 : 10)) / 2;
    draw_logo(stdscr, y, (cols - logo_width(!big)) / 2, !big);
    y += big ? 4 : 2;
    attron(COLOR_PAIR(CP_HINT));
    centre(y, cols, "chess in your terminal");
    attroff(COLOR_PAIR(CP_HINT));
    y += 2;
    if (big) {
        static const int row[8] = { 9, 7, 8, 10, 11, 8, 7, 9 };   /* ♜♞♝♛♚♝♞♜ */
        int x = (cols - 16) / 2;
        for (int i = 0; i < 8; i++)
            render_piece(stdscr, y, x + i * 2, row[i], COLOR_PAIR(CP_TITLE) | A_BOLD);
        y += 2;
    }

    int x = (cols - 36) / 2;
    char field[PLAYER_NAME_MAX + 8];
    attron(COLOR_PAIR(CP_LABEL));
    mvaddstr(y, x, "Your name");
    mvaddstr(y + 1, x, "Theme");
    attroff(COLOR_PAIR(CP_LABEL));
    snprintf(field, sizeof(field), "%s▏", name);
    attron(COLOR_PAIR(CP_INFO_VAL) | A_BOLD);
    mvw_fit(stdscr, y, x + 12, 24, field);
    snprintf(field, sizeof(field), "◂ %s ▸", theme_name(s->theme));
    mvw_fit(stdscr, y + 1, x + 12, 24, field);
    attroff(COLOR_PAIR(CP_INFO_VAL) | A_BOLD);
    y += 3;
    attron(COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
    centre(y, cols, "⏎ continue");
    attroff(COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
    if (msg[0]) {
        attron(COLOR_PAIR(msg_err ? CP_STATUS_ERR : CP_HINT));
        centre(y + 2, cols, msg);
        attroff(COLOR_PAIR(msg_err ? CP_STATUS_ERR : CP_HINT));
    }
    attron(COLOR_PAIR(CP_HINT));
    mvw_fit(stdscr, rows - 1, 1, cols - 2, "type your name  ←→ theme  ⏎ go  esc quit");
    attroff(COLOR_PAIR(CP_HINT));
    refresh();
}

static int finish(TUIState *s, const char *name, char *msg, size_t n)
{
    ProfileList probe;
    memset(&probe, 0, sizeof(probe));
    if (!profiles_add(&probe, &s->engines, name, msg, n)) return 0;
    DchessStats old;
    stats_load(&old);
    char games[512];
    records_path(games, sizeof(games));
    if (!profiles_first_run(&s->profiles, name, &old, games, theme_name(s->theme))) {
        snprintf(msg, n, "Could not save profiles.conf");
        return 0;
    }
    s->file_active = s->profiles.active;
    if (s->players[WHITE].kind == PLAYER_HUMAN && !s->players[WHITE].name[0])
        s->players[WHITE] = player_profile(s->profiles.p[s->profiles.active].name);
    return 1;
}

int tui_welcome(TUIState *s)
{
    char name[PLAYER_NAME_MAX + 1] = "", msg[96] = "";
    const char *user = getenv("USER");
    if (user && strlen(user) <= PLAYER_NAME_MAX) snprintf(name, sizeof(name), "%s", user);
    int len = (int)strlen(name), err = 0;
    keypad(stdscr, TRUE);
    for (;;) {
        draw(s, name, msg, err);
        wint_t ch;
        int kind = get_wch(&ch);
        if (kind == ERR) continue;
        if (kind == OK && ch == 27) return 0;
        if ((kind == OK && (ch == '\n' || ch == '\r')) || (kind == KEY_CODE_YES && ch == KEY_ENTER)) {
            if (finish(s, name, msg, sizeof(msg))) return 1;
            err = 1;
            continue;
        }
        msg[0] = '\0';
        if (kind == KEY_CODE_YES && (ch == KEY_LEFT || ch == KEY_RIGHT)) {
            int n = theme_count();
            s->theme = (s->theme + (ch == KEY_RIGHT ? 1 : n - 1)) % n;
            init_colors(s->theme);
        } else if ((kind == KEY_CODE_YES && ch == KEY_BACKSPACE) || (kind == OK && (ch == 127 || ch == 8))) {
            while (len && ((unsigned char)name[len - 1] & 0xC0) == 0x80) len--;
            if (len) len--;
            name[len] = '\0';
        } else if (kind == OK && ch >= 32) {
            char mb[MB_LEN_MAX];
            mbstate_t st;
            memset(&st, 0, sizeof(st));
            size_t n = wcrtomb(mb, (wchar_t)ch, &st);
            if (n != (size_t)-1 && len + (int)n <= PLAYER_NAME_MAX) {
                memcpy(name + len, mb, n);
                len += (int)n;
                name[len] = '\0';
            }
        }
    }
}
