#include "tui/replay_tui.h"
#include "tui/colors.h"
#include "tui/render.h"
#include "engine/fen.h"
#include "utils/theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void goto_ply(TUIState *s, int k)
{
    const ReplayGame *g = s->replay;
    game_reset(&s->game);
    if (g->fen[0]) game_load_fen(&s->game, g->fen);
    for (int i = 0; i < k; i++) {
        game_play(&s->game, g->moves[i]);
        game_update_status(&s->game);
    }
    s->replay_ply = k;
}

static void step(TUIState *s, int dir)
{
    const ReplayGame *g = s->replay;
    if (dir > 0 && s->replay_ply < g->count) {
        game_play(&s->game, g->moves[s->replay_ply++]);
        game_update_status(&s->game);
    } else if (dir < 0 && s->replay_ply > 0) {
        goto_ply(s, s->replay_ply - 1);
    }
}

/* The current position becomes a new game with the active profile's
 * remembered setup; 0 when the game is already over there. */
static int play_from_here(TUIState *s)
{
    if (s->game.game_over) {
        snprintf(s->status, sizeof(s->status), "The game is over at this move");
        return 0;
    }
    CliArgs a;
    memset(&a, 0, sizeof(a));
    a.no_menu = 1;
    position_to_fen(&s->game.pos, s->game.halfmove_clock, s->game.move_count / 2 + 1,
                    a.fen, sizeof(a.fen));
    a.players[WHITE] = player_human();
    a.players[BLACK] = player_builtin(DIFF_MEDIUM);
    if (s->profiles.count) {
        const Profile *p = &s->profiles.p[s->profiles.active];
        a.players[WHITE] = tui_word_player(s, p->white, player_profile(p->name));
        a.players[BLACK] = tui_word_player(s, p->black, player_builtin(DIFF_MEDIUM));
        int t = p->theme[0] ? theme_from_name(p->theme) : -1;
        if (t >= 0) { a.theme = t; a.theme_set = 1; }
        snprintf(a.profile, sizeof(a.profile), "%s", p->name);
    }
    if (!a.theme_set) { a.theme = s->theme; a.theme_set = 1; }
    int move = s->game.move_count / 2 + 1;
    tui_init(s, &a);
    game_update_status(&s->game);   /* a finished position shows as finished */
    snprintf(s->status, sizeof(s->status), "Playing on from move %d", move);
    return 1;
}

int replay_open(TUIState *s, const char *path, long offset)
{
    char err[128];
    ReplayGame *g = malloc(sizeof(*g));
    if (!g) return 0;
    if (!replay_read(path, offset, g, err, sizeof(err))) {
        snprintf(s->status, sizeof(s->status), "%s", err);
        free(g);
        return 0;
    }
    s->replay = g;
    s->replay_auto = 0;
    s->selected = 0;
    memset(s->highlight, 0, sizeof(s->highlight));
    goto_ply(s, 0);
    snprintf(s->status, sizeof(s->status), "%s", g->err);

    Screen *sc = tui_screen_open(s);
    if (!sc) { free(g); s->replay = NULL; return 0; }
    int result = 0;
    for (;;) {
        WINDOW *in = tui_screen_input(sc);   /* a resize replaces the window */
        tui_screen_paint(sc);
        wtimeout(in, s->replay_auto ? 1000 : -1);
        int ch = wgetch(in);
        if (ch == ERR) {
            step(s, 1);
            if (s->replay_ply >= g->count) s->replay_auto = 0;
            continue;
        }
        if (ch != KEY_RESIZE) s->status[0] = '\0';
        if (ch == 27) break;
        switch (ch) {
        case KEY_RESIZE:            tui_screen_resize(sc); break;
        case KEY_LEFT:  case 'h':   step(s, -1); break;
        case KEY_RIGHT: case 'l':   step(s, 1); break;
        case KEY_HOME:  case 'g':   goto_ply(s, 0); break;
        case KEY_END:   case 'G':   goto_ply(s, g->count); break;
        case ' ':
            s->replay_auto = !s->replay_auto && s->replay_ply < g->count;
            break;
        case 'p':
            result = play_from_here(s);
            break;
        default: break;
        }
        if (result) break;
    }
    wtimeout(tui_screen_input(sc), 100);
    tui_screen_close(sc);
    free(g);
    if (!result) s->replay = NULL;   /* play-from-here's tui_init cleared it */
    return result;
}

static void draw_picker(const ReplayList *l, const char *path, int sel, int top)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();
    const char *base = strrchr(path, '/');
    char line[768];
    snprintf(line, sizeof(line), "dchess · replay · %s · %d games", base ? base + 1 : path, l->count);
    attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvw_fit(stdscr, 0, 1, cols - 2, line);
    attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);

    int pw = cols > 70 ? (cols - 34) / 2 : cols - 34;
    if (pw < 12) pw = 12;
    for (int r = 0; r < rows - 3 && top + r < l->count; r++) {
        const ReplayEntry *e = &l->e[top + r];
        int on = top + r == sel, x = 1;
        if (on) attron(A_REVERSE);
        snprintf(line, sizeof(line), "%5d  ", top + r + 1);
        mvw_fit(stdscr, 2 + r, x, 7, line);
        x += 7;
        snprintf(line, sizeof(line), "%s – %s", e->white[0] ? e->white : "?", e->black[0] ? e->black : "?");
        mvw_fit(stdscr, 2 + r, x, pw < cols - x - 1 ? pw : cols - x - 1, line);
        x += pw + 1;
        if (x + 8 < cols) { mvw_fit(stdscr, 2 + r, x, 8, e->result); x += 9; }
        if (x + 11 < cols) { mvw_fit(stdscr, 2 + r, x, 10, e->date); x += 11; }
        if (x < cols - 1) mvw_fit(stdscr, 2 + r, x, cols - 1 - x, e->event);
        if (on) attroff(A_REVERSE);
    }
    attron(COLOR_PAIR(CP_HINT));
    mvw_fit(stdscr, rows - 1, 1, cols - 2, "↑↓ move  pgup/pgdn page  ⏎ open  esc quit");
    attroff(COLOR_PAIR(CP_HINT));
    refresh();
}

int replay_browse(TUIState *s, ReplayList *l, const char *path)
{
    if (l->count == 1) return replay_open(s, path, l->e[0].offset);
    int sel = 0, top = 0;
    keypad(stdscr, TRUE);
    for (;;) {
        int page = getmaxy(stdscr) - 3;
        if (page < 1) page = 1;
        if (sel < top) top = sel;
        if (sel >= top + page) top = sel - page + 1;
        draw_picker(l, path, sel, top);
        int ch = getch();
        switch (ch) {
        case KEY_UP:    case 'k': if (sel) sel--; break;
        case KEY_DOWN:  case 'j': if (sel < l->count - 1) sel++; break;
        case KEY_PPAGE: sel = sel > page ? sel - page : 0; break;
        case KEY_NPAGE: sel = sel + page < l->count ? sel + page : l->count - 1; break;
        case KEY_HOME:  sel = 0; break;
        case KEY_END:   sel = l->count - 1; break;
        case 27:        return 0;
        case '\n': case '\r': case KEY_ENTER:
            if (replay_open(s, path, l->e[sel].offset)) return 1;
            break;
        default: break;
        }
    }
}
