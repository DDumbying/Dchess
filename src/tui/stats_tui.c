#include "tui/stats_tui.h"
#include "tui/colors.h"
#include "tui/panels.h"
#include "tui/panel.h"
#include "tui/render.h"
#include "game/statsview.h"
#include "utils/dash.h"
#include "utils/theme.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { FOCUS_OPP, FOCUS_RECENT };

typedef struct {
    ProfileList *profiles;
    int          index;           /* profile shown */
    RecordList   rec;
    StatsView    v;
    int          focus, opp_top, rec_top;
} Page;

static const char *BARS[] = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };

/* Prints at most `width` characters of UTF-8 text. */
static void put_clip(WINDOW *w, int row, int col, int width, const char *s)
{
    int chars = 0;
    const char *end = s;
    while (*end) {
        if (((unsigned char)*end & 0xC0) != 0x80) {
            if (chars == width) break;
            chars++;
        }
        end++;
    }
    mvwprintw(w, row, col, "%.*s", (int)(end - s), s);
}

static int pct(int part, int whole) { return whole ? 100 * part / whole : 0; }

static int pct_pair(int p)
{
    return p >= 55 ? CP_STATUS_OK : p >= 45 ? CP_ACC_CLOCK : CP_STATUS_ERR;
}

static void age(long ts, char *buf, size_t n)
{
    long d = (long)time(NULL) - ts;
    if (ts <= 0)            snprintf(buf, n, "?");
    else if (d < 60)        snprintf(buf, n, "now");
    else if (d < 3600)      snprintf(buf, n, "%ldm", d / 60);
    else if (d < 86400)     snprintf(buf, n, "%ldh", d / 3600);
    else if (d < 7 * 86400) snprintf(buf, n, "%ldd", d / 86400);
    else                    snprintf(buf, n, "%ldw", d / (7 * 86400));
}

static WINDOW *panel(int h, int w, int y, int x, const char *title, int accent)
{
    if (h < 3 || w < 8) return NULL;
    WINDOW *p = derwin(stdscr, h, w, y, x);
    if (!p) return NULL;
    wbkgd(p, COLOR_PAIR(CP_CANVAS));
    werase(p);
    panel_frame(p, title, accent);
    return p;
}

static void colored(WINDOW *w, int pair, int bold, int row, int col, const char *fmt, int v)
{
    wattron(w, COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
    mvwprintw(w, row, col, fmt, v);
    wattroff(w, COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
}

static void sparkline(WINDOW *w, int row, int col, int width, const float *t, int n)
{
    int first = n > width ? n - width : 0;
    for (int i = first; i < n; i++) {
        int lv = (int)(t[i] * 7.0f + 0.5f);
        int pair = CP_RAMP_BASE + dash_ramp_index(lv, 8, 8);
        wattron(w, COLOR_PAIR(pair));
        mvwprintw(w, row, col + i - first, "%s", BARS[lv]);
        wattroff(w, COLOR_PAIR(pair));
    }
}

static void draw_summary(Page *pg, int h, int w, int y, int x)
{
    char title[160];
    snprintf(title, sizeof(title), "%s · %d games", pg->profiles->p[pg->index].name, pg->v.total.games);
    WINDOW *p = panel(h, w, y, x, title, CP_ACC_BOARD);
    if (!p) return;
    const StatsView *v = &pg->v;
    colored(p, CP_STATUS_OK, 1, 1, 2, "%dW", v->total.wins);
    colored(p, CP_ACC_CLOCK, 1, 1, 8, "%dD", v->total.draws);
    colored(p, CP_STATUS_ERR, 1, 1, 14, "%dL", v->total.losses);
    int wp = pct(v->total.wins, v->total.games);
    wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, 1, 21, "win"); wattroff(p, COLOR_PAIR(CP_HINT));
    colored(p, pct_pair(wp), 1, 1, 25, "%d%%", wp);
    if (h > 3) {
        int ww = pct(v->as_white.wins, v->as_white.games), bw = pct(v->as_black.wins, v->as_black.games);
        wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, 2, 2, "as White"); wattroff(p, COLOR_PAIR(CP_HINT));
        colored(p, pct_pair(ww), 0, 2, 11, "%d%%", ww);
        wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, 2, 17, "as Black"); wattroff(p, COLOR_PAIR(CP_HINT));
        colored(p, pct_pair(bw), 0, 2, 26, "%d%%", bw);
    }
    delwin(p);
}

static void draw_trend(Page *pg, int h, int w, int y, int x)
{
    WINDOW *p = panel(h, w, y, x, "trend", CP_ACC_EVAL);
    if (!p) return;
    const StatsView *v = &pg->v;
    if (!v->trend_count) {
        wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, 1, 2, "no games yet"); wattroff(p, COLOR_PAIR(CP_HINT));
        delwin(p);
        return;
    }
    sparkline(p, 1, 2, w - 4, v->trend, v->trend_count);
    if (h > 3) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, 2, 2, "streak %c%d  best W%d  per week %d %d %d %d",
                  v->streak ? v->streak : '-', v->streak_len, v->best_win_streak,
                  v->per_week[0], v->per_week[1], v->per_week[2], v->per_week[3]);
        wattroff(p, COLOR_PAIR(CP_HINT));
    }
    delwin(p);
}

static void draw_opponents(Page *pg, int h, int w, int y, int x)
{
    WINDOW *p = panel(h, w, y, x, "opponents", pg->focus == FOCUS_OPP ? CP_ACC_MOVES : CP_HINT);
    if (!p) return;
    const StatsView *v = &pg->v;
    int name_w = w - 34 > 10 ? w - 34 : 10, rows = h - 3;
    wattron(p, COLOR_PAIR(CP_HINT));
    mvwprintw(p, 1, 2, "%-*s %5s  %-9s %4s %5s", name_w, "OPPONENT", "GAMES", "W-D-L", "WIN", "LAST");
    wattroff(p, COLOR_PAIR(CP_HINT));
    if (pg->opp_top > v->opp_count - rows) pg->opp_top = v->opp_count - rows > 0 ? v->opp_count - rows : 0;
    for (int i = 0; i < rows && pg->opp_top + i < v->opp_count; i++) {
        const SvOpponent *o = &v->opp[pg->opp_top + i];
        char wdl[24], when[16];
        snprintf(wdl, sizeof(wdl), "%d-%d-%d", o->wins, o->draws, o->losses);
        if (o->last) age(o->last, when, sizeof(when));
        else snprintf(when, sizeof(when), "-");
        put_clip(p, 2 + i, 2, name_w, o->name);
        mvwprintw(p, 2 + i, 3 + name_w, "%5d  %-9s", o->games, wdl);
        int wp = pct(o->wins, o->games);
        colored(p, pct_pair(wp), 0, 2 + i, 18 + name_w, "%3d%%", wp);
        wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, 2 + i, 24 + name_w, "%4s", when); wattroff(p, COLOR_PAIR(CP_HINT));
    }
    if (!v->opp_count) {
        wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, 2, 2, "no games yet"); wattroff(p, COLOR_PAIR(CP_HINT));
    }
    delwin(p);
}

static void draw_endings(Page *pg, int h, int w, int y, int x)
{
    WINDOW *p = panel(h, w, y, x, "endings", CP_ACC_CLOCK);
    if (!p) return;
    const StatsView *v = &pg->v;
    const char *names[] = { "mate", "resign", "stalemate", "repetition", "50-move", "material" };
    int counts[] = { v->mates, v->resigns, v->stalemates, v->repetitions, v->fifty, v->material };
    int pairs[]  = { CP_STATUS_OK, CP_STATUS_ERR, CP_ACC_CLOCK, CP_ACC_CLOCK, CP_ACC_CLOCK, CP_ACC_CLOCK };
    int most = 1, bar = w - 20 > 4 ? w - 20 : 4, row = 1;
    for (int i = 0; i < 6; i++) if (counts[i] > most) most = counts[i];
    for (int i = 0; i < 6 && row < h - 2; i++, row++) {
        int fill = dash_bar_fill(counts[i], most, bar);
        wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, row, 2, "%-10s", names[i]); wattroff(p, COLOR_PAIR(CP_HINT));
        wattron(p, COLOR_PAIR(pairs[i]));
        for (int c = 0; c < bar; c++) mvwprintw(p, row, 13 + c, "%s", c < fill ? "█" : "░");
        wattroff(p, COLOR_PAIR(pairs[i]));
        mvwprintw(p, row, 14 + bar, "%3d", counts[i]);
    }
    if (row < h - 1) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, h - 2, 2, "avg %d moves · %dm%02ds",
                  (v->avg_plies + 1) / 2, v->avg_seconds / 60, v->avg_seconds % 60);
        wattroff(p, COLOR_PAIR(CP_HINT));
    }
    delwin(p);
}

static const char *ending_word(const char *e)
{
    if (!strcmp(e, "checkmate"))  return "mate";
    if (!strcmp(e, "resigned"))   return "resign";
    if (!strcmp(e, "repetition")) return "repet.";
    if (!strcmp(e, "fifty-move")) return "50-move";
    if (!strcmp(e, "legacy"))     return "old";
    return e;
}

static void draw_recent(Page *pg, int h, int w, int y, int x)
{
    WINDOW *p = panel(h, w, y, x, "recent", pg->focus == FOCUS_RECENT ? CP_ACC_MOVES : CP_HINT);
    if (!p) return;
    const StatsView *v = &pg->v;
    const char *who = pg->profiles->p[pg->index].name;
    int rows = h - 2, name_w = w - 28 > 6 ? w - 28 : 6;
    if (pg->rec_top > v->recent_count - rows) pg->rec_top = v->recent_count - rows > 0 ? v->recent_count - rows : 0;
    for (int i = 0; i < rows && pg->rec_top + i < v->recent_count; i++) {
        const Record *r = v->recent[pg->rec_top + i];
        int white = r->white_kind == KIND_PROFILE && !strcmp(r->white, who);
        int o = white ? r->result : -r->result;
        char when[16];
        age(records_time(r), when, sizeof(when));
        int pair = o > 0 ? CP_STATUS_OK : o < 0 ? CP_STATUS_ERR : CP_ACC_CLOCK;
        wattron(p, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(p, 1 + i, 2, "%c", o > 0 ? 'W' : o < 0 ? 'L' : 'D');
        wattroff(p, COLOR_PAIR(pair) | A_BOLD);
        put_clip(p, 1 + i, 4, name_w, white ? r->black : r->white);
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, 1 + i, 5 + name_w, "%3d mv  %-7s %4s", (r->plies + 1) / 2, ending_word(r->end_reason), when);
        wattroff(p, COLOR_PAIR(CP_HINT));
    }
    if (!v->recent_count) {
        wattron(p, COLOR_PAIR(CP_HINT)); mvwprintw(p, 1, 2, "no games yet"); wattroff(p, COLOR_PAIR(CP_HINT));
    }
    delwin(p);
}

static void draw(Page *pg)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    bkgd(COLOR_PAIR(CP_CANVAS));
    erase();
    int body = rows - 1;

    if (cols >= 80 && rows >= 24) {
        int half = cols / 2, oh = (body - 5) / 2;
        draw_summary(pg, 5, half, 0, 0);
        draw_trend(pg, 5, cols - half, 0, half);
        draw_opponents(pg, oh, cols, 5, 0);
        draw_endings(pg, body - 5 - oh, 34, 5 + oh, 0);
        draw_recent(pg, body - 5 - oh, cols - 34, 5 + oh, 34);
    } else {
        int oh = (body - 4) / 2;
        draw_summary(pg, 4, cols, 0, 0);
        draw_opponents(pg, oh, cols, 4, 0);
        draw_recent(pg, body - 4 - oh, cols, 4 + oh, 0);
    }
    attron(COLOR_PAIR(CP_HINT));
    mvprintw(rows - 1, 1, "%.*s", cols - 2, "←→ profile  tab panel  ↑↓ scroll  esc back");
    attroff(COLOR_PAIR(CP_HINT));
    refresh();
}

static void rebuild(Page *pg)
{
    stats_view_build(&pg->rec, &pg->profiles->p[pg->index], (long)time(NULL), &pg->v);
    pg->opp_top = pg->rec_top = 0;
}

static void run(ProfileList *profiles, int start)
{
    Page pg;
    char games[512];
    memset(&pg, 0, sizeof(pg));
    pg.profiles = profiles;
    pg.index = start;
    records_path(games, sizeof(games));
    records_load(games, &pg.rec);
    rebuild(&pg);
    keypad(stdscr, TRUE);

    for (;;) {
        draw(&pg);
        int ch = getch();
        int n = profiles->count;
        if (ch == 27 || ch == 'q') break;
        if (ch == '\t') pg.focus = pg.focus == FOCUS_OPP ? FOCUS_RECENT : FOCUS_OPP;
        else if ((ch == KEY_LEFT || ch == 'h') && n > 1) { pg.index = (pg.index + n - 1) % n; rebuild(&pg); }
        else if ((ch == KEY_RIGHT || ch == 'l') && n > 1) { pg.index = (pg.index + 1) % n; rebuild(&pg); }
        else if (ch == KEY_UP || ch == 'k') {
            int *top = pg.focus == FOCUS_OPP ? &pg.opp_top : &pg.rec_top;
            if (*top > 0) (*top)--;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (pg.focus == FOCUS_OPP) pg.opp_top++;
            else pg.rec_top++;
        }
    }
    records_free(&pg.rec);
    clear();
}

void stats_screen(TUIState *s)
{
    if (!s->profiles.count) return;
    run(&s->profiles, s->profiles.active);
}

void stats_standalone(const char *profile)
{
    ProfileList l;
    if (!profiles_load(&l) || !l.count) {
        printf("No profiles yet. Play a game first.\n");
        return;
    }
    int i = profile && profile[0] ? profiles_find(&l, profile) : -1;
    int theme = theme_from_name(l.p[i >= 0 ? i : l.active].theme);

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    init_colors(theme >= 0 ? theme : 0);
    run(&l, i >= 0 ? i : l.active);
    endwin();
}

void stats_mini(WINDOW *parent, const TUIState *s)
{
    if (!s->profiles.count) return;
    const Profile *pr = &s->profiles.p[s->profiles.active];
    RecordList rec;
    StatsView v;
    char games[512], title[160];
    records_path(games, sizeof(games));
    records_load(games, &rec);
    stats_view_build(&rec, pr, (long)time(NULL), &v);
    records_free(&rec);

    int ph, pw, py, px;
    getmaxyx(parent, ph, pw);
    getbegyx(parent, py, px);
    int h = 7, w = pw - 4 < 44 ? pw - 4 : 44;
    if (h > ph || w < 20) return;
    int y = py + (ph - h) / 2, x = px + (pw - w) / 2;

    WINDOW *shadow = panel_shadow(h, w, y, x);
    WINDOW *p = newwin(h, w, y, x);
    wbkgd(p, COLOR_PAIR(CP_CANVAS));
    snprintf(title, sizeof(title), "%s · %d games", pr->name, v.total.games);
    panel_frame(p, title, CP_ACC_BOARD);
    colored(p, CP_STATUS_OK, 1, 2, 2, "%dW", v.total.wins);
    colored(p, CP_ACC_CLOCK, 1, 2, 8, "%dD", v.total.draws);
    colored(p, CP_STATUS_ERR, 1, 2, 14, "%dL", v.total.losses);
    int wp = pct(v.total.wins, v.total.games);
    colored(p, pct_pair(wp), 1, 2, 21, "%d%%", wp);
    sparkline(p, 3, 2, w - 4, v.trend, v.trend_count);
    wattron(p, COLOR_PAIR(CP_HINT));
    mvwprintw(p, 4, 2, "streak %c%d · best W%d", v.streak ? v.streak : '-', v.streak_len, v.best_win_streak);
    mvwprintw(p, 5, 2, "any key closes");
    wattroff(p, COLOR_PAIR(CP_HINT));
    wrefresh(p);
    wgetch(p);
    delwin(p);
    panel_shadow_destroy(shadow);
}
