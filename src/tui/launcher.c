#include "tui/launcher.h"
#include "tui/colors.h"
#include "tui/commands.h"
#include "tui/engines_tui.h"
#include "tui/panels.h"
#include "tui/render.h"
#include "tui/stats_tui.h"
#include "engine/fen.h"
#include "game/records.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include "utils/dash.h"
#include "utils/theme.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum { ROW_WHITE, ROW_BLACK, ROW_POSITION, ROW_THEME, ROW_START, ROW_COUNT };
enum { FOCUS_PROFILES, FOCUS_GAME };

#define LEFT_W  24
#define GAME_ROWS  10

typedef struct {
    Player     sel[2];
    int        use_custom_fen;
    char       fen[128];
    int        theme;
    int        row, focus, pcursor;   /* pcursor == count: "+ new profile" */
    char       msg[96];
    int        msg_err;
    RecordList rec;
    char       games[512];
} Launch;

static const char *active_name(const TUIState *s)
{
    return s->profiles.count ? s->profiles.p[s->profiles.active].name : "";
}

/* Active profile, the other profiles, Guest, dchess E/M/H, then engines. */
static int cycle_count(const TUIState *s) { return s->profiles.count + 4 + s->engines.count; }

static Player cycle_player(const TUIState *s, int i)
{
    const char *names[PROFILES_MAX];
    int n = profiles_names(&s->profiles, names, PROFILES_MAX);
    if (i < n) return player_profile(names[i]);
    i -= n;
    if (i == 0) return player_human();
    if (i <= 3) return player_builtin(i - 1);
    return player_uci(s->engines.e[i - 4].name);
}

static int cycle_index(const TUIState *s, const Player *p)
{
    for (int i = 0; i < cycle_count(s); i++) {
        Player q = cycle_player(s, i);
        if (q.kind != p->kind) continue;
        if (p->kind == PLAYER_BUILTIN ? q.level == p->level : strcmp(q.name, p->name) == 0)
            return i;
    }
    return 0;
}

static Player word_player(const TUIState *s, const char *w, Player fallback)
{
    if (!w[0]) return fallback;
    if (strcasecmp(w, "guest") == 0) return player_human();
    int lv = players_level_from_name(w);
    if (lv >= 0) return player_builtin(lv);
    if (profiles_find(&s->profiles, w) >= 0) return player_profile(w);
    if (engines_find(&s->engines, w)) return player_uci(w);
    return fallback;
}

static void say(Launch *L, int err, const char *text)
{
    snprintf(L->msg, sizeof(L->msg), "%s", text);
    L->msg_err = err;
}

static void reload(Launch *L)
{
    records_free(&L->rec);
    records_load(L->games, &L->rec);
}

/* Brings back the profile's theme and last White/Black setup. */
static void apply_profile(TUIState *s, Launch *L)
{
    const Profile *p = &s->profiles.p[s->profiles.active];
    L->sel[WHITE] = word_player(s, p->white, player_profile(p->name));
    L->sel[BLACK] = word_player(s, p->black, player_builtin(DIFF_MEDIUM));
    int t = p->theme[0] ? theme_from_name(p->theme) : -1;
    if (t >= 0 && t != L->theme) {
        L->theme = t;
        init_colors(t);
    }
}

static void set_active(TUIState *s, Launch *L, int i)
{
    s->profiles.active = i;
    L->pcursor = i;
    profiles_save(&s->profiles);
    apply_profile(s, L);
}

static void fix_selection(TUIState *s, Launch *L)
{
    for (int side = WHITE; side <= BLACK; side++)
        L->sel[side] = cycle_player(s, cycle_index(s, &L->sel[side]));
}

static void age(long ts, char *buf, size_t n)
{
    long d = (long)time(NULL) - ts;
    if (ts <= 0)               snprintf(buf, n, "?");
    else if (d < 60)           snprintf(buf, n, "now");
    else if (d < 3600)         snprintf(buf, n, "%ldm", d / 60);
    else if (d < 86400)        snprintf(buf, n, "%ldh", d / 3600);
    else if (d < 7 * 86400)    snprintf(buf, n, "%ldd", d / 86400);
    else                       snprintf(buf, n, "%ldw", d / (7 * 86400));
}

static RecordTally tally(const Launch *L, const Profile *p)
{
    RecordTally t = records_tally(&L->rec, p->name);
    t.games  += p->legacy_games;
    t.wins   += p->legacy_wins;
    t.losses += p->legacy_losses;
    t.draws  += p->legacy_draws;
    return t;
}

static WINDOW *panel(int h, int w, int y, int x, const char *title, int focused)
{
    WINDOW *p = derwin(stdscr, h, w, y, x);
    if (!p) return NULL;
    wbkgd(p, COLOR_PAIR(CP_CANVAS));
    werase(p);
    panel_frame(p, title, focused ? CP_ACC_BOARD : CP_HINT);
    return p;
}

static void draw_profiles(TUIState *s, Launch *L, int h, int y)
{
    WINDOW *p = panel(h, LEFT_W, y, 0, "profiles", L->focus == FOCUS_PROFILES);
    if (!p) return;
    int rows = h - 2, top = L->pcursor >= rows ? L->pcursor - rows + 1 : 0;
    for (int i = top; i <= s->profiles.count && i - top < rows; i++) {
        int on = L->focus == FOCUS_PROFILES && i == L->pcursor;
        if (on) wattron(p, A_REVERSE);
        if (i == s->profiles.count) {
            wattron(p, COLOR_PAIR(CP_HINT));
            mvwprintw(p, 1 + i - top, 1, " %-*s", LEFT_W - 3, "+ new profile");
            wattroff(p, COLOR_PAIR(CP_HINT));
        } else {
            const Profile *pr = &s->profiles.p[i];
            RecordTally t = tally(L, pr);
            char wdl[24];
            snprintf(wdl, sizeof(wdl), "%d-%d-%d", t.wins, t.draws, t.losses);
            int act = i == s->profiles.active;
            if (act) wattron(p, COLOR_PAIR(CP_STATUS_OK) | A_BOLD);
            mvwprintw(p, 1 + i - top, 1, "%s%-*.*s%*s", act ? "▸" : " ",
                      LEFT_W - 12, LEFT_W - 12, pr->name, 9, wdl);
            if (act) wattroff(p, COLOR_PAIR(CP_STATUS_OK) | A_BOLD);
        }
        if (on) wattroff(p, A_REVERSE);
    }
    delwin(p);
}

static void draw_recent(TUIState *s, Launch *L, int h, int y)
{
    WINDOW *p = panel(h, LEFT_W, y, 0, "recent", 0);
    if (!p) return;
    const Record *r[8];
    int max = h - 2 < 8 ? h - 2 : 8;
    int n = max > 0 ? records_recent(&L->rec, active_name(s), r, max) : 0;
    if (!n) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, 1, 2, "no games yet");
        wattroff(p, COLOR_PAIR(CP_HINT));
    }
    for (int i = 0; i < n; i++) {
        int mine_white = r[i]->white_kind == KIND_PROFILE && strcmp(r[i]->white, active_name(s)) == 0;
        int o = mine_white ? r[i]->result : -r[i]->result;
        const char *opp = mine_white ? r[i]->black : r[i]->white;
        char when[16];
        age(records_time(r[i]), when, sizeof(when));
        int pair = o > 0 ? CP_STATUS_OK : o < 0 ? CP_STATUS_ERR : CP_ACC_CLOCK;
        wattron(p, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(p, 1 + i, 2, "%c", o > 0 ? 'W' : o < 0 ? 'L' : 'D');
        wattroff(p, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(p, 1 + i, 4, "%-*.*s", LEFT_W - 11, LEFT_W - 11, opp);
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, 1 + i, LEFT_W - 5, "%3s", when);
        wattroff(p, COLOR_PAIR(CP_HINT));
    }
    delwin(p);
}

static void draw_game(TUIState *s, Launch *L, int h, int w, int y, int x, int small)
{
    char title[64];
    if (small) snprintf(title, sizeof(title), "new game · %s", active_name(s));
    else       snprintf(title, sizeof(title), "new game");
    WINDOW *p = panel(h, w, y, x, title, L->focus == FOCUS_GAME);
    if (!p) return;

    static const char *names[] = { "White", "Black", "Pos  ", "Theme" };
    int val_w = w - 16;
    if (val_w < 6) val_w = 6;
    for (int r = 0; r < ROW_START; r++) {
        char label[160];
        if (r <= ROW_BLACK) {
            player_label(&L->sel[r], label, sizeof(label));
            const EngineEntry *e = L->sel[r].kind == PLAYER_UCI ? engines_find(&s->engines, L->sel[r].name) : NULL;
            if (e) {
                char st[48];
                size_t len = strlen(label);
                engine_strength_label(e, st, sizeof(st));
                snprintf(label + len, sizeof(label) - len, " · %s", st);
            }
        } else if (r == ROW_POSITION) {
            snprintf(label, sizeof(label), "%s", L->use_custom_fen
                     ? (L->fen[0] ? L->fen : "<enter to type a FEN>") : "Standard");
        } else {
            snprintf(label, sizeof(label), "%s", theme_name(L->theme));
        }
        int on = L->focus == FOCUS_GAME && L->row == r;
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, 2 + r, 3, "%s", names[r]);
        wattroff(p, COLOR_PAIR(CP_HINT));
        if (on) wattron(p, A_REVERSE);
        mvwprintw(p, 2 + r, 10, "%s %-*.*s %s", on ? "◂" : " ", val_w, val_w, label, on ? "▸" : " ");
        if (on) wattroff(p, A_REVERSE);
    }
    int on = L->focus == FOCUS_GAME && L->row == ROW_START;
    wattron(p, COLOR_PAIR(CP_STATUS_OK) | A_BOLD | (on ? A_REVERSE : 0));
    mvwprintw(p, h - 2, (w - 18) / 2 > 1 ? (w - 18) / 2 : 1, "  ▶ START GAME  ");
    wattroff(p, COLOR_PAIR(CP_STATUS_OK) | A_BOLD | (on ? A_REVERSE : 0));
    delwin(p);
}

static void draw_winrate(TUIState *s, Launch *L, int y, int x, int w)
{
    static const char *bars[] = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
    float v[30];
    int n = records_winrate(&L->rec, active_name(s), v, 30);
    if (w < 16) return;
    wattron(stdscr, COLOR_PAIR(CP_HINT));
    mvprintw(y, x, "win rate ");
    wattroff(stdscr, COLOR_PAIR(CP_HINT));
    if (!n) return;
    for (int i = 0; i < n && 9 + i < w - 6; i++) {
        int lv = (int)(v[i] * 7.0f + 0.5f);
        attron(COLOR_PAIR(CP_RAMP_BASE + dash_ramp_index(lv, 8, 8)));
        mvprintw(y, x + 9 + i, "%s", bars[lv]);
        attroff(COLOR_PAIR(CP_RAMP_BASE + dash_ramp_index(lv, 8, 8)));
    }
    RecordTally t = tally(L, &s->profiles.p[s->profiles.active]);
    attron(COLOR_PAIR(CP_STATUS_OK) | A_BOLD);
    mvprintw(y, x + 10 + (n < w - 15 ? n : w - 15), "%d%%", t.games ? 100 * t.wins / t.games : 0);
    attroff(COLOR_PAIR(CP_STATUS_OK) | A_BOLD);
}

static void draw(TUIState *s, Launch *L)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int small = cols < 80 || rows < 20;
    bkgd(COLOR_PAIR(CP_CANVAS));
    erase();

    if (small) {
        int h = GAME_ROWS < rows - 3 ? GAME_ROWS : rows - 3;
        draw_game(s, L, h, cols, 0, 0, 1);
        if (rows > h + 3) draw_winrate(s, L, h + 1, 2, cols - 2);
    } else {
        int ph = s->profiles.count + 3;
        if (ph > (rows - 1) / 2) ph = (rows - 1) / 2;
        draw_profiles(s, L, ph, 0);
        draw_recent(s, L, rows - 1 - ph, ph);
        draw_game(s, L, GAME_ROWS, cols - LEFT_W, 0, LEFT_W, 0);
        draw_winrate(s, L, GAME_ROWS + 1, LEFT_W + 2, cols - LEFT_W - 2);
    }

    int pair = L->msg_err ? CP_STATUS_ERR : CP_STATUS_OK;
    attron(COLOR_PAIR(pair));
    mvprintw(rows - 2, small ? 2 : LEFT_W + 2, "%.*s", cols - LEFT_W - 4 > 10 ? cols - LEFT_W - 4 : cols - 4, L->msg);
    attroff(COLOR_PAIR(pair));
    attron(COLOR_PAIR(CP_HINT));
    mvprintw(rows - 1, 1, "%.*s", cols - 2, small
             ? "↑↓ move  ←→ change  p profile  e engines  s stats  esc quit"
             : "tab panel  ↑↓ move  ←→ change  n new  r rename  d delete  e engines  s stats  esc quit");
    attroff(COLOR_PAIR(CP_HINT));
    refresh();
}

/* Edits `buf` on the message row. 1 on Enter, 0 on Esc. */
static int prompt(const char *label, char *buf, size_t size)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int len = (int)strlen(buf), col = 2 + (int)strlen(label);
    curs_set(1);
    for (;;) {
        move(rows - 2, 0);
        clrtoeol();
        attron(COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
        mvprintw(rows - 2, 2, "%s", label);
        attroff(COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
        mvprintw(rows - 2, col, "%.*s", cols - col - 1, buf);
        move(rows - 2, col + len);
        refresh();
        int ch = getch();
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

static void new_profile(TUIState *s, Launch *L)
{
    char name[PROFILE_NAME_MAX + 1] = "", err[96];
    if (!prompt("New profile: ", name, sizeof(name)) || !name[0]) { say(L, 0, "Cancelled"); return; }
    if (!profiles_add(&s->profiles, &s->engines, name, err, sizeof(err))) { say(L, 1, err); return; }
    set_active(s, L, s->profiles.count - 1);
    snprintf(err, sizeof(err), "Welcome, %s", name);
    say(L, 0, err);
}

static void rename_profile(TUIState *s, Launch *L)
{
    int i = L->pcursor;
    char name[PROFILE_NAME_MAX + 1], err[96];
    snprintf(name, sizeof(name), "%s", s->profiles.p[i].name);
    if (!prompt("Rename to: ", name, sizeof(name))) { say(L, 0, "Cancelled"); return; }
    if (strcmp(name, s->profiles.p[i].name) == 0) return;
    if (!profiles_rename(&s->profiles, &s->engines, i, name, L->games, err, sizeof(err))) {
        say(L, 1, err);
        return;
    }
    profiles_save(&s->profiles);
    reload(L);
    apply_profile(s, L);
    say(L, 0, "Renamed");
}

static void delete_profile(TUIState *s, Launch *L)
{
    int i = L->pcursor;
    char q[96];
    if (s->profiles.count == 1) { say(L, 1, "The last profile cannot be deleted"); return; }
    snprintf(q, sizeof(q), "Delete %s? Its games stay in the history. y to confirm", s->profiles.p[i].name);
    say(L, 1, q);
    draw(s, L);
    if (getch() != 'y') { say(L, 0, "Kept"); return; }
    profiles_remove(&s->profiles, i);
    set_active(s, L, s->profiles.active);
    fix_selection(s, L);
    say(L, 0, "Deleted");
}

static void show_stats(TUIState *s)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    tui_refresh_stats(s);
    WINDOW *sw = newwin(rows, cols, 0, 0);
    keypad(sw, TRUE);
    draw_stats_overlay(sw, &s->stats);
    doupdate();
    wgetch(sw);
    delwin(sw);
    clear();
}

static void change_row(TUIState *s, Launch *L, int dir)
{
    if (L->row <= ROW_BLACK) {
        int c = cycle_count(s), i = cycle_index(s, &L->sel[L->row]);
        L->sel[L->row] = cycle_player(s, (i + c + dir) % c);
    } else if (L->row == ROW_POSITION) {
        L->use_custom_fen = !L->use_custom_fen;
    } else if (L->row == ROW_THEME) {
        L->theme = (L->theme + theme_count() + dir) % theme_count();
        init_colors(L->theme);
    }
}

static int start(TUIState *s, Launch *L)
{
    CliArgs chosen;
    memset(&chosen, 0, sizeof(chosen));
    chosen.players[WHITE] = L->sel[WHITE];
    chosen.players[BLACK] = L->sel[BLACK];
    chosen.theme = L->theme;
    snprintf(chosen.profile, sizeof(chosen.profile), "%s", active_name(s));
    if (L->use_custom_fen && L->fen[0])
        snprintf(chosen.fen, sizeof(chosen.fen), "%s", L->fen);
    records_free(&L->rec);
    tui_init(s, &chosen);
    s->show_onboarding = 0;
    return 1;
}

int tui_launcher(TUIState *state)
{
    Launch L;
    memset(&L, 0, sizeof(L));
    L.theme = state->theme;
    L.focus = FOCUS_GAME;
    L.row = ROW_START;
    L.sel[WHITE] = state->players[WHITE];
    L.sel[BLACK] = state->players[BLACK];
    L.pcursor = state->profiles.active;
    records_path(L.games, sizeof(L.games));
    records_load(L.games, &L.rec);
    if (state->profiles.count && !state->cli_setup) apply_profile(state, &L);
    fix_selection(state, &L);
    keypad(stdscr, TRUE);

    for (;;) {
        draw(state, &L);
        int rows, cols, ch = getch();
        getmaxyx(stdscr, rows, cols);
        int small = cols < 80 || rows < 20;
        if (small) L.focus = FOCUS_GAME;
        say(&L, 0, "");

        switch (ch) {
        case 27:
            records_free(&L.rec);
            return 0;
        case '\t':
            if (!small) L.focus = L.focus == FOCUS_GAME ? FOCUS_PROFILES : FOCUS_GAME;
            break;
        case 'e': case 'E':
            engines_screen(&state->engines);
            engines_load(&state->engines);
            fix_selection(state, &L);
            clear();
            break;
        case 's': case 'S':
            show_stats(state);
            break;
        case 'p': case 'P':
            if (small && state->profiles.count)
                set_active(state, &L, (state->profiles.active + 1) % state->profiles.count);
            break;
        default:
            break;
        }

        if (L.focus == FOCUS_PROFILES) {
            int n = state->profiles.count;
            if ((ch == KEY_UP || ch == 'k') && L.pcursor > 0) {
                L.pcursor--;
                set_active(state, &L, L.pcursor);
            } else if ((ch == KEY_DOWN || ch == 'j') && L.pcursor < n) {
                L.pcursor++;
                if (L.pcursor < n) set_active(state, &L, L.pcursor);
            } else if (ch == 'n' || ((ch == '\n' || ch == '\r' || ch == KEY_ENTER) && L.pcursor == n)) {
                new_profile(state, &L);
            } else if (ch == 'r' && L.pcursor < n) {
                rename_profile(state, &L);
            } else if (ch == 'd' && L.pcursor < n) {
                delete_profile(state, &L);
            }
            continue;
        }

        switch (ch) {
        case KEY_UP: case 'k':    L.row = (L.row + ROW_COUNT - 1) % ROW_COUNT; break;
        case KEY_DOWN: case 'j':  L.row = (L.row + 1) % ROW_COUNT; break;
        case KEY_LEFT: case 'h':  change_row(state, &L, -1); break;
        case KEY_RIGHT: case 'l': change_row(state, &L, +1); break;
        case '\n': case '\r': case KEY_ENTER:
            if (L.row == ROW_START) return start(state, &L);
            if (L.row == ROW_POSITION && L.use_custom_fen) {
                char fen[128];
                snprintf(fen, sizeof(fen), "%s", L.fen);
                Position probe;
                if (prompt("FEN: ", fen, sizeof(fen)) && parse_fen(fen, &probe, NULL, NULL))
                    snprintf(L.fen, sizeof(L.fen), "%s", fen);
                else if (!L.fen[0])
                    L.use_custom_fen = 0;
            } else {
                L.row = ROW_START;
            }
            break;
        default:
            break;
        }
    }
}
