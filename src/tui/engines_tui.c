#include "tui/engines_tui.h"
#include "tui/colors.h"
#include "tui/panels.h"
#include "tui/panel.h"
#include "game/uci.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static const int TIME_STEPS[] = { 100, 200, 500, 1000, 2000, 3000, 5000, 10000, 30000, 60000 };
#define N_TIME_STEPS ((int)(sizeof(TIME_STEPS) / sizeof(TIME_STEPS[0])))

typedef struct {
    WINDOW *win, *shadow;
    int     h, w;
    int     cursor;       /* list->count is the "+ Add engine…" row */
    char    msg[160];
    int     msg_err;
} EngScreen;

static void build(EngScreen *s)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    s->w = cols - 2 < 72 ? cols - 2 : 72;
    s->h = rows - 2 < 20 ? rows - 2 : 20;
    if (s->w < 1) s->w = 1;
    if (s->h < 1) s->h = 1;
    int r = (rows - s->h) / 2, c = (cols - s->w) / 2;

    werase(stdscr);
    wrefresh(stdscr);
    s->shadow = panel_shadow(s->h, s->w, r, c);
    s->win = newwin(s->h, s->w, r, c);
    wbkgd(s->win, COLOR_PAIR(CP_CANVAS));
    keypad(s->win, TRUE);
}

static void destroy(EngScreen *s)
{
    delwin(s->win);
    panel_shadow_destroy(s->shadow);
}

static void say(EngScreen *s, int err, const char *text)
{
    snprintf(s->msg, sizeof(s->msg), "%s", text);
    s->msg_err = err;
}

/* Paths are ASCII in practice; a long one ends in "…". */
static void clip(const char *src, char *dst, size_t size, int width)
{
    size_t len = strlen(src);
    if ((int)len <= width && len < size) {
        memcpy(dst, src, len + 1);
        return;
    }
    int keep = width > 1 ? width - 1 : 0;
    if (keep > (int)size - 4) keep = (int)size - 4;
    memcpy(dst, src, (size_t)keep);
    memcpy(dst + keep, "…", sizeof("…"));
}

static void blank(EngScreen *s, int row)
{
    for (int c = 1; c < s->w - 1; c++) mvwaddch(s->win, row, c, ' ');
}

static void draw(EngScreen *s, const EngineList *l)
{
    WINDOW *w = s->win;
    int inner = s->w - 4, name_w = 20, str_w = 16;
    int path_w = inner - name_w - str_w - 4;
    if (path_w < 4) path_w = 4;

    werase(w);
    panel_frame(w, "dchess · engines", CP_ACC_BOARD);
    wattron(w, COLOR_PAIR(CP_HINT));
    mvwprintw(w, 1, 4, "%-*s %-*s %s", name_w, "NAME", str_w, "STRENGTH", "PATH");
    wattroff(w, COLOR_PAIR(CP_HINT));

    int first = 2, last = s->h - 5;
    int visible = last - first + 1;
    int top = s->cursor >= visible ? s->cursor - visible + 1 : 0;
    for (int i = top; i <= l->count && first + (i - top) <= last; i++) {
        int row = first + (i - top), on = (i == s->cursor);
        if (on) wattron(w, A_REVERSE);
        if (i == l->count) {
            mvwprintw(w, row, 2, "%-*s", inner, on ? "▸ + Add engine…" : "  + Add engine…");
        } else {
            char str[48], path[256];
            engine_strength_label(&l->e[i], str, sizeof(str));
            clip(l->e[i].path, path, sizeof(path), path_w);
            mvwprintw(w, row, 2, "%s %-*.*s %-*.*s %-*s", on ? "▸" : " ",
                      name_w, name_w, l->e[i].name, str_w, str_w, str, path_w, path);
        }
        if (on) wattroff(w, A_REVERSE);
    }

    int pair = s->msg_err ? CP_STATUS_ERR : CP_STATUS_OK;
    wattron(w, COLOR_PAIR(pair));
    mvwprintw(w, s->h - 4, 2, "%-.*s", inner, s->msg);
    wattroff(w, COLOR_PAIR(pair));
    wattron(w, COLOR_PAIR(CP_HINT));
    mvwprintw(w, s->h - 3, 2, "%-.*s", inner, "a add   enter edit   t test   d delete");
    mvwprintw(w, s->h - 2, 2, "%-.*s", inner, "esc back");
    wattroff(w, COLOR_PAIR(CP_HINT));
    wrefresh(w);
}

/* Edits `buf` in place. 1 on Enter, 0 on Esc. */
static int prompt(EngScreen *s, int row, const char *label, char *buf, size_t size)
{
    WINDOW *w = s->win;
    int len = (int)strlen(buf), lab = (int)strlen(label);
    int width = s->w - 4 - lab;
    curs_set(1);
    for (;;) {
        blank(s, row);
        wattron(w, A_BOLD);
        mvwprintw(w, row, 2, "%s", label);
        wattroff(w, A_BOLD);
        int shown = len > width - 1 ? len - (width - 1) : 0;   /* keep the end visible */
        mvwprintw(w, row, 2 + lab, "%s", buf + shown);
        wmove(w, row, 2 + lab + len - shown);
        wrefresh(w);

        int ch = wgetch(w);
        if (ch == 27) { curs_set(0); return 0; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) { curs_set(0); return 1; }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len) buf[--len] = '\0';
        } else if (ch >= 32 && ch < 127 && len < (int)size - 1) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }
}

static int nearest_time(int ms)
{
    int best = 0;
    for (int i = 1; i < N_TIME_STEPS; i++) {
        int d = TIME_STEPS[i] - ms, bd = TIME_STEPS[best] - ms;
        if ((d < 0 ? -d : d) < (bd < 0 ? -bd : bd)) best = i;
    }
    return best;
}

/* ←/→ switches time and depth, ↑/↓ changes the value. */
static int pick_limit(EngScreen *s, int row, EngineEntry *e)
{
    int depth_mode = e->limit_depth > 0;
    int ti = nearest_time(e->limit_ms ? e->limit_ms : 1000);
    int depth = e->limit_depth ? e->limit_depth : 12;
    say(s, 0, "←→ time or depth   ↑↓ value   enter ok   esc cancel");
    for (;;) {
        EngineEntry tmp = *e;
        tmp.elo = 0;
        tmp.limit_depth = depth_mode ? depth : 0;
        tmp.limit_ms = TIME_STEPS[ti];
        char label[48];
        engine_strength_label(&tmp, label, sizeof(label));
        blank(s, row);
        mvwprintw(s->win, row, 2, "Limit:  ◂ %s ▸", label);
        wattron(s->win, COLOR_PAIR(CP_STATUS_OK));
        blank(s, s->h - 4);
        mvwprintw(s->win, s->h - 4, 2, "%-.*s", s->w - 4, s->msg);
        wattroff(s->win, COLOR_PAIR(CP_STATUS_OK));
        wrefresh(s->win);

        switch (wgetch(s->win)) {
        case KEY_LEFT: case KEY_RIGHT: case 'h': case 'l':
            depth_mode = !depth_mode;
            break;
        case KEY_UP: case 'k':
            if (depth_mode) { if (depth < 30) depth++; }
            else if (ti < N_TIME_STEPS - 1) ti++;
            break;
        case KEY_DOWN: case 'j':
            if (depth_mode) { if (depth > 1) depth--; }
            else if (ti > 0) ti--;
            break;
        case '\n': case '\r': case KEY_ENTER:
            e->limit_depth = depth_mode ? depth : 0;
            e->limit_ms = TIME_STEPS[ti];
            return 1;
        case 27:
            return 0;
        }
    }
}

/* off, the minimum, each hundred above it, the maximum. */
static int elo_steps(const UciProbe *p, int *out, int max)
{
    int n = 0;
    out[n++] = 0;
    if (p->elo_max <= p->elo_min) {
        out[n++] = p->elo_min;
        return n;
    }
    for (int v = p->elo_min; v <= p->elo_max && n < max; v = (v / 100 + 1) * 100)
        out[n++] = v;
    if (n < max && out[n - 1] != p->elo_max) out[n++] = p->elo_max;
    return n;
}

static int pick_elo(EngScreen *s, int row, EngineEntry *e, const UciProbe *p)
{
    int steps[64], n = elo_steps(p, steps, 64), i = 0;
    for (int k = 1; k < n; k++)
        if (e->elo && steps[k] <= e->elo) i = k;
    for (;;) {
        blank(s, row);
        if (steps[i]) mvwprintw(s->win, row, 2, "Elo:    ◂ %d ▸", steps[i]);
        else          mvwprintw(s->win, row, 2, "Elo:    ◂ off ▸");
        wrefresh(s->win);
        switch (wgetch(s->win)) {
        case KEY_LEFT: case 'h': if (i > 0) i--; break;
        case KEY_RIGHT: case 'l': if (i < n - 1) i++; break;
        case '\n': case '\r': case KEY_ENTER: e->elo = steps[i]; return 1;
        case 27: return 0;
        }
    }
}

/* Path → probe → name → limit → Elo. 1 when `e` is ready to save. */
static int form(EngScreen *s, const EngineList *l, int index, EngineEntry *e)
{
    int r_path = s->h - 8, r_name = s->h - 7, r_limit = s->h - 6, r_elo = s->h - 5;
    UciProbe probe;
    char err[160];

    for (;;) {
        draw(s, l);
        for (int r = r_path; r <= r_elo; r++) blank(s, r);
        if (!prompt(s, r_path, "Path:   ", e->path, sizeof(e->path))) return 0;
        if (!e->path[0]) continue;
        say(s, 0, "Testing…");
        draw(s, l);
        for (int r = r_path + 1; r <= r_elo; r++) blank(s, r);
        mvwprintw(s->win, r_path, 2, "Path:   %s", e->path);
        wrefresh(s->win);
        if (uci_probe(e->path, &probe, err, sizeof(err))) break;
        say(s, 1, err);
    }

    say(s, 0, "");
    if (!e->name[0])
        snprintf(e->name, sizeof(e->name), "%.40s", probe.name[0] ? probe.name : "Engine");
    for (;;) {
        draw(s, l);
        for (int r = r_path; r <= r_elo; r++) blank(s, r);
        mvwprintw(s->win, r_path, 2, "Path:   %s", e->path);
        if (!prompt(s, r_name, "Name:   ", e->name, sizeof(e->name))) return 0;
        if (engines_check_name(l, e->name, index, err, sizeof(err))) break;
        say(s, 1, err);
    }

    if (!pick_limit(s, r_limit, e)) return 0;
    if (probe.elo_supported) {
        if (!pick_elo(s, r_elo, e, &probe)) return 0;
    } else {
        e->elo = 0;
    }
    return 1;
}

static void save(EngScreen *s, const EngineList *l, const char *done)
{
    char path[512], m[160];
    if (engines_save(l)) {
        say(s, 0, done);
        return;
    }
    engines_path(path, sizeof(path));
    snprintf(m, sizeof(m), "Could not write %.140s", path);
    say(s, 1, m);
}

static void add(EngScreen *s, EngineList *l)
{
    EngineEntry e;
    char err[160], done[96];
    if (l->count >= ENGINES_MAX) {
        say(s, 1, "The list is full (32 engines)");
        return;
    }
    memset(&e, 0, sizeof(e));
    e.limit_ms = 1000;
    if (!form(s, l, -1, &e)) { say(s, 0, "Cancelled"); return; }
    if (!engines_add(l, &e, err, sizeof(err))) { say(s, 1, err); return; }
    s->cursor = l->count - 1;
    snprintf(done, sizeof(done), "Added %s", e.name);
    save(s, l, done);
}

static void edit(EngScreen *s, EngineList *l, int i)
{
    EngineEntry e = l->e[i];
    char err[160], done[96];
    if (!form(s, l, i, &e)) { say(s, 0, "Cancelled"); return; }
    if (!engines_replace(l, i, &e, err, sizeof(err))) { say(s, 1, err); return; }
    snprintf(done, sizeof(done), "Saved %s", e.name);
    save(s, l, done);
}

static void test_entry(EngScreen *s, const EngineList *l, const EngineEntry *e)
{
    UciProbe p;
    char err[160], m[160];
    say(s, 0, "Testing…");
    draw(s, l);
    if (!uci_probe(e->path, &p, err, sizeof(err))) {
        snprintf(m, sizeof(m), "✗ %.150s", err);
        say(s, 1, m);
        return;
    }
    int len = snprintf(m, sizeof(m), "✓ %s", p.name[0] ? p.name : e->name);
    if (p.author[0] && len < (int)sizeof(m))
        len += snprintf(m + len, sizeof(m) - len, " · by %s", p.author);
    if (p.elo_supported && len < (int)sizeof(m))
        snprintf(m + len, sizeof(m) - len, " · Elo %d–%d", p.elo_min, p.elo_max);
    say(s, 0, m);
}

static void remove_entry(EngScreen *s, EngineList *l)
{
    char m[96], name[ENGINE_NAME_MAX + 1];
    snprintf(name, sizeof(name), "%s", l->e[s->cursor].name);
    snprintf(m, sizeof(m), "Delete %s? y to confirm", name);
    say(s, 1, m);
    draw(s, l);
    if (wgetch(s->win) != 'y') { say(s, 0, "Kept"); return; }
    engines_remove(l, name);
    if (s->cursor > l->count) s->cursor = l->count;
    snprintf(m, sizeof(m), "Deleted %s", name);
    save(s, l, m);
}

void engines_screen(EngineList *list)
{
    EngScreen s;
    memset(&s, 0, sizeof(s));
    build(&s);
    if (!list->count) say(&s, 0, "No engines yet — press a to add one");

    for (;;) {
        draw(&s, list);
        int ch = wgetch(s.win);
        switch (ch) {
        case KEY_RESIZE: destroy(&s); build(&s); break;
        case KEY_UP: case 'k': if (s.cursor > 0) s.cursor--; break;
        case KEY_DOWN: case 'j': if (s.cursor < list->count) s.cursor++; break;
        case 'a': add(&s, list); break;
        case '\n': case '\r': case KEY_ENTER:
            if (s.cursor == list->count) add(&s, list);
            else edit(&s, list, s.cursor);
            break;
        case 't': if (s.cursor < list->count) test_entry(&s, list, &list->e[s.cursor]); break;
        case 'd': if (s.cursor < list->count) remove_entry(&s, list); break;
        case 27: case 'q': destroy(&s); return;
        }
    }
}
