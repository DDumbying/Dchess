#include "tui/launcher.h"
#include "tui/art.h"
#include "tui/colors.h"
#include "tui/commands.h"
#include "tui/engines_tui.h"
#include "tui/panels.h"
#include "tui/render.h"
#include "tui/replay_tui.h"
#include "tui/stats_tui.h"
#include "engine/fen.h"
#include "game/records.h"
#include "game/statsview.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include "utils/dash.h"
#include "utils/text.h"
#include "utils/theme.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <limits.h>
#include <wchar.h>

enum { ROW_WHITE, ROW_BLACK, ROW_POSITION, ROW_BOOK, ROW_THEME, ROW_START, ROW_COUNT };
enum { FOCUS_PROFILES, FOCUS_GAME, FOCUS_CARD };

#define LEFT_W  24
#define CARD_W  28
#define BLOCK_H  14      /* panel height of the dashboard block */
#define BLOCK_W  132
#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define GAME_ROWS  10

typedef struct {
    Player     sel[2];
    int        use_custom_fen;
    char       fen[128];
    int        theme;
    char       book[256], custom_book[256];   /* builtin, off or a path */
    char       book_start[256];               /* changed from this = remembered */
    int        row, focus, pcursor;   /* pcursor == count: "+ new profile" */
    int        csel, card_rows;       /* the card's selected game; how many it shows */
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


static void say(Launch *L, int err, const char *text)
{
    snprintf(L->msg, sizeof(L->msg), "%.*s", (int)sizeof(L->msg) - 1, text);
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
    L->sel[WHITE] = tui_word_player(s, p->white, player_profile(p->name));
    L->sel[BLACK] = tui_word_player(s, p->black, player_builtin(DIFF_MEDIUM));
    if (!s->cli_book[0]) {
        snprintf(L->book, sizeof(L->book), "%s", p->book[0] ? p->book : "builtin");
        snprintf(L->book_start, sizeof(L->book_start), "%s", L->book);
        L->custom_book[0] = '\0';
        if (strcmp(L->book, "builtin") && strcmp(L->book, "off"))
            snprintf(L->custom_book, sizeof(L->custom_book), "%s", L->book);
    }
    int t = p->theme[0] ? theme_from_name(p->theme) : -1;
    if (t >= 0 && t != L->theme) {
        L->theme = t;
        init_colors(t);
    }
}

static void set_active(TUIState *s, Launch *L, int i)
{
    s->profiles.active = i;
    s->file_active = i;
    L->pcursor = i;
    L->csel = 0;
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

static void draw_profiles(TUIState *s, Launch *L, int h, int y, int x)
{
    WINDOW *p = panel(h, LEFT_W, y, x, "profiles", L->focus == FOCUS_PROFILES);
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
            mvwprintw(p, 1 + i - top, 1, "%s", act ? "▸" : " ");
            mvw_fit(p, 1 + i - top, 2, LEFT_W - 12, pr->name);
            mvwprintw(p, 1 + i - top, LEFT_W - 10, "%9s", wdl);
            if (act) wattroff(p, COLOR_PAIR(CP_STATUS_OK) | A_BOLD);
        }
        if (on) wattroff(p, A_REVERSE);
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

    static const char *names[] = { "White", "Black", "Pos  ", "Book ", "Theme" };
    int board = !small && w >= 16 + 19 + 12 && h >= 12;
    int top = board ? 1 + (h - 2 - 11) / 2 : 1, fy = board ? top + 2 : 2;
    int val_w = w - 16 - (board ? 19 : 0);
    if (val_w < 6) val_w = 6;
    for (int r = 0; r < ROW_START; r++) {
        char label[300];
        if (r <= ROW_BLACK) {
            player_label(&L->sel[r], label, sizeof(label));
            const EngineEntry *e = L->sel[r].kind == PLAYER_UCI ? engines_find(&s->engines, L->sel[r].name) : NULL;
            if (e) {
                char st[48];
                size_t len = strlen(label);
                engine_strength_label(e, st, sizeof(st));
                snprintf(label + len, sizeof(label) - len, " · %s", st);
            }
        } else if (r == ROW_BOOK) {
            const char *slash = strrchr(L->book, '/');
            snprintf(label, sizeof(label), "%s", !strcmp(L->book, "builtin") ? "built-in"
                     : slash ? slash + 1 : L->book);
        } else if (r == ROW_POSITION) {
            snprintf(label, sizeof(label), "%s", L->use_custom_fen
                     ? (L->fen[0] ? L->fen : "<enter to type a FEN>") : "Standard");
        } else {
            snprintf(label, sizeof(label), "%s", theme_name(L->theme));
        }
        int on = L->focus == FOCUS_GAME && L->row == r;
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, fy + r, 3, "%s", names[r]);
        wattroff(p, COLOR_PAIR(CP_HINT));
        if (on) wattron(p, A_REVERSE);
        mvwprintw(p, fy + r, 10, "%s ", on ? "◂" : " ");
        mvw_fit(p, fy + r, 12, val_w, label);
        mvwprintw(p, fy + r, 12 + val_w, " %s", on ? "▸" : " ");
        if (on) wattroff(p, A_REVERSE);
    }
    if (board) {
        Position pos;
        const char *fen = L->use_custom_fen && L->fen[0] ? L->fen : START_FEN;
        if (parse_fen(fen, &pos, NULL, NULL) || parse_fen(START_FEN, &pos, NULL, NULL))
            draw_mini_board(p, top, w - 19, &pos);
    }
    int on = L->focus == FOCUS_GAME && L->row == ROW_START;
    wattron(p, COLOR_PAIR(CP_STATUS_OK) | A_BOLD | (on ? A_REVERSE : 0));
    mvwprintw(p, board ? top + 10 : h - 2, (w - 18) / 2 > 1 ? (w - 18) / 2 : 1, "  ▶ START GAME  ");
    wattroff(p, COLOR_PAIR(CP_STATUS_OK) | A_BOLD | (on ? A_REVERSE : 0));
    delwin(p);
}

/* Built by draw_card; ⏎ on the card reads it. */
static StatsView card_view;

static void draw_card(TUIState *s, Launch *L, int h, int y, int x)
{
    const Profile *pr = &s->profiles.p[s->profiles.active];
    WINDOW *p = panel(h, CARD_W, y, x, pr->name, L->focus == FOCUS_CARD);
    if (!p) return;
    stats_view_build(&L->rec, pr, (long)time(NULL), &card_view);
    L->card_rows = 0;
    int in = CARD_W - 4;
    RecordTally t = card_view.total;
    if (!t.games) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_fit(p, 1, 2, in, "no games yet");
        mvw_fit(p, 2, 2, in, "⏎ to play");
        wattroff(p, COLOR_PAIR(CP_HINT));
        delwin(p);
        return;
    }
    char line[64];
    snprintf(line, sizeof(line), "%dW %dL %dD", t.wins, t.losses, t.draws);
    wattron(p, COLOR_PAIR(CP_INFO_VAL) | A_BOLD);
    mvw_fit(p, 1, 2, in, line);
    wattroff(p, COLOR_PAIR(CP_INFO_VAL) | A_BOLD);
    int pct = 100 * t.wins / t.games, bar = in - 5, fill = bar * pct / 100;
    wattron(p, COLOR_PAIR(CP_STATUS_OK));
    for (int i = 0; i < bar; i++) mvwaddstr(p, 2, 2 + i, i < fill ? "█" : "░");
    wattroff(p, COLOR_PAIR(CP_STATUS_OK));
    mvwprintw(p, 2, 2 + bar, " %3d%%", pct);
    int row = 3;
    if (card_view.streak) {
        snprintf(line, sizeof(line), "streak %c%d", card_view.streak, card_view.streak_len);
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_fit(p, row++, 2, in, line);
        wattroff(p, COLOR_PAIR(CP_HINT));
    }
    row++;
    for (int i = 0; i < card_view.recent_count && row < h - 1; i++, row++) {
        const Record *r = card_view.recent[i];
        if (L->focus == FOCUS_CARD && i == L->csel) {
            wattron(p, A_REVERSE);
            mvwprintw(p, row, 1, "%*s", CARD_W - 2, "");
            wattroff(p, A_REVERSE);
        }
        int mine_white = r->white_kind == KIND_PROFILE && strcmp(r->white, pr->name) == 0;
        int o = mine_white ? r->result : -r->result;
        char opp[PLAYER_NAME_MAX + 1], when[16];
        stats_opponent_name(r, mine_white ? WHITE : BLACK, opp, sizeof(opp));
        age(records_time(r), when, sizeof(when));
        int pair = o > 0 ? CP_STATUS_OK : o < 0 ? CP_STATUS_ERR : CP_ACC_CLOCK;
        wattron(p, COLOR_PAIR(pair) | A_BOLD);
        mvwprintw(p, row, 2, "%c", o > 0 ? 'W' : o < 0 ? 'L' : 'D');
        wattroff(p, COLOR_PAIR(pair) | A_BOLD);
        mvw_fit(p, row, 4, in - 8, opp);
        wattron(p, COLOR_PAIR(CP_HINT));
        mvwprintw(p, row, CARD_W - 5, "%3s", when);
        wattroff(p, COLOR_PAIR(CP_HINT));
        L->card_rows = i + 1;
    }
    delwin(p);
}

static void draw_header(TUIState *s, Launch *L, int hdr, int y, int x, int cols)
{
    draw_logo(stdscr, y, x + 1, hdr == 1);
    char who[160];
    snprintf(who, sizeof(who), "%s · %s", active_name(s), theme_name(L->theme));
    int w = text_width(who), room = cols - logo_width(hdr == 1) - 4;
    if (room < 8) return;
    if (w > room) w = room;
    attron(COLOR_PAIR(CP_HINT));
    mvw_fit(stdscr, y + (hdr == 3), x + cols - 1 - w, w, who);
    attroff(COLOR_PAIR(CP_HINT));
}

static void draw(TUIState *s, Launch *L)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int small = cols < 80 || rows < 20;
    int hdr = !small && rows >= 22 ? 3 : 1;
    bkgd(COLOR_PAIR(CP_CANVAS));
    erase();

    int bh = rows - 2 - hdr, x0 = 0, y0 = 0, w = cols;
    if (small) {
        draw_header(s, L, hdr, 0, 0, cols);
        draw_game(s, L, GAME_ROWS < bh ? GAME_ROWS : bh, cols, hdr, 0, 1);
    } else {
        /* One block, centred: header, a blank row, then the panels. */
        int ph = bh - 1 < BLOCK_H ? bh - 1 : BLOCK_H;
        if (w > BLOCK_W) { x0 = (cols - BLOCK_W) / 2; w = BLOCK_W; }
        y0 = (rows - 2 - hdr - 1 - ph) / 2;
        if (y0 < 0) y0 = 0;
        draw_header(s, L, hdr, y0, x0, w);
        int py = y0 + hdr + 1, card = w >= 100 && s->profiles.count;
        int gw = w - LEFT_W - (card ? CARD_W : 0);
        draw_profiles(s, L, ph, py, x0);
        draw_game(s, L, ph, gw, py, x0 + LEFT_W, 0);
        if (card) draw_card(s, L, ph, py, x0 + w - CARD_W);
        else L->card_rows = 0;
        y0 = py + ph;
    }

    int pair = L->msg_err ? CP_STATUS_ERR : CP_STATUS_OK;
    attron(COLOR_PAIR(pair));
    if (small) mvw_fit(stdscr, rows - 2, 2, cols - 4, L->msg);
    else if (y0 < rows - 1) mvw_fit(stdscr, y0, x0 + LEFT_W + 2, w - LEFT_W - 4, L->msg);
    attroff(COLOR_PAIR(pair));
    attron(COLOR_PAIR(CP_HINT));
    mvw_fit(stdscr, rows - 1, 1, cols - 2, small
             ? "⏎ play  ↑↓ move  ←→ change  p profile  e engines  s stats  esc quit"
             : "⏎ play  tab panel  ↑↓ move  ←→ change  n new  r rename  d delete  e engines  s stats  esc quit");
    attroff(COLOR_PAIR(CP_HINT));
    refresh();
}

static int prompt(const char *label, char *buf, size_t size)
{
    int rows = getmaxy(stdscr);
    int len = (int)strlen(buf), col = 2 + (int)strlen(label);
    curs_set(1);
    for (;;) {
        move(rows - 2, 0);
        clrtoeol();
        attron(COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
        mvprintw(rows - 2, 2, "%s", label);
        attroff(COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
        mvprintw(rows - 2, col, "%s", buf);
        refresh();
        wint_t ch;
        int kind = get_wch(&ch);
        if (kind == ERR) continue;
        if (kind == OK && ch == 27) { curs_set(0); return 0; }
        if ((kind == OK && (ch == '\n' || ch == '\r')) || (kind == KEY_CODE_YES && ch == KEY_ENTER)) {
            curs_set(0);
            return 1;
        }
        if ((kind == KEY_CODE_YES && ch == KEY_BACKSPACE) || (kind == OK && (ch == 127 || ch == 8))) {
            while (len && ((unsigned char)buf[len - 1] & 0xC0) == 0x80) len--;   /* whole UTF-8 character */
            if (len) len--;
            buf[len] = '\0';
        } else if (kind == OK && ch >= 32 && ch != 127) {
            char mb[MB_LEN_MAX];
            mbstate_t st;
            memset(&st, 0, sizeof(st));
            size_t n = wcrtomb(mb, (wchar_t)ch, &st);
            if (n != (size_t)-1 && len + (int)n < (int)size) {
                memcpy(buf + len, mb, n);
                len += (int)n;
                buf[len] = '\0';
            }
        }
    }
}

static void new_profile(TUIState *s, Launch *L)
{
    char name[PLAYER_NAME_MAX + 1] = "", err[96];
    if (!prompt("New profile: ", name, sizeof(name)) || !name[0]) { say(L, 0, "Cancelled"); return; }
    if (!profiles_add(&s->profiles, &s->engines, name, err, sizeof(err))) { say(L, 1, err); return; }
    set_active(s, L, s->profiles.count - 1);
    snprintf(err, sizeof(err), "Welcome, %s", name);
    say(L, 0, err);
}

static void rename_profile(TUIState *s, Launch *L)
{
    int i = L->pcursor;
    char name[PLAYER_NAME_MAX + 1], err[96];
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

static void change_row(TUIState *s, Launch *L, int dir)
{
    if (L->row <= ROW_BLACK) {
        int c = cycle_count(s), i = cycle_index(s, &L->sel[L->row]);
        L->sel[L->row] = cycle_player(s, (i + c + dir) % c);
    } else if (L->row == ROW_POSITION) {
        L->use_custom_fen = !L->use_custom_fen;
    } else if (L->row == ROW_BOOK) {
        const char *opts[3] = { "builtin", "off", L->custom_book };
        int n = L->custom_book[0] ? 3 : 2, i = 0;
        while (i < n && strcmp(L->book, opts[i])) i++;
        snprintf(L->book, sizeof(L->book), "%s", opts[(i + n + dir) % n]);
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
    chosen.theme_set = 1;
    snprintf(chosen.book, sizeof(chosen.book), "%s", s->cli_book);
    snprintf(chosen.profile, sizeof(chosen.profile), "%s", active_name(s));
    if (L->use_custom_fen && L->fen[0])
        snprintf(chosen.fen, sizeof(chosen.fen), "%s", L->fen);
    records_free(&L->rec);
    tui_init(s, &chosen);
    if (strcmp(L->book, L->book_start)) {   /* chosen here, so remembered like the book command */
        snprintf(s->book_choice, sizeof(s->book_choice), "%s", L->book);
        s->cli_book[0] = '\0';
    }
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
    snprintf(L.book, sizeof(L.book), "%s", state->book_choice);
    snprintf(L.book_start, sizeof(L.book_start), "%s", L.book);
    if (strcmp(L.book, "builtin") && strcmp(L.book, "off"))
        snprintf(L.custom_book, sizeof(L.custom_book), "%s", L.book);
    records_path(L.games, sizeof(L.games));
    records_load(L.games, &L.rec);
    if (state->profiles.count && !state->cli_setup) apply_profile(state, &L);
    fix_selection(state, &L);
    if (state->first_run == 2) say(&L, 1, "Could not save profiles.conf; this profile lasts for this run");
    keypad(stdscr, TRUE);

    for (;;) {
        draw(state, &L);
        int rows, cols, ch = getch();
        getmaxyx(stdscr, rows, cols);
        int small = cols < 80 || rows < 20;
        int card = !small && (cols < BLOCK_W ? cols : BLOCK_W) >= 100 && state->profiles.count;
        if (small || (L.focus == FOCUS_CARD && !card)) L.focus = FOCUS_GAME;
        say(&L, 0, "");

        switch (ch) {
        case 27:
            records_free(&L.rec);
            return 0;
        case '\t':
            if (!small)
                L.focus = L.focus == FOCUS_PROFILES ? FOCUS_GAME
                        : L.focus == FOCUS_GAME && card ? FOCUS_CARD : FOCUS_PROFILES;
            break;
        case 'e': case 'E':
            engines_screen(&state->engines);
            engines_load(&state->engines);
            fix_selection(state, &L);
            clear();
            break;
        case 's': case 'S':
            if (stats_screen(state, 1)) {
                records_free(&L.rec);
                return 1;
            }
            break;
        case 'p': case 'P':
            if (small && state->profiles.count)
                set_active(state, &L, (state->profiles.active + 1) % state->profiles.count);
            break;
        default:
            break;
        }

        if (L.focus == FOCUS_CARD) {
            if ((ch == KEY_UP || ch == 'k') && L.csel > 0) L.csel--;
            else if ((ch == KEY_DOWN || ch == 'j') && L.csel < L.card_rows - 1) L.csel++;
            else if ((ch == '\n' || ch == '\r' || ch == KEY_ENTER) && L.csel < L.card_rows) {
                const Record *r = card_view.recent[L.csel];
                if (r->legacy) {
                    say(&L, 1, "no moves saved");
                } else if (replay_open(state, L.games, r->offset)) {
                    records_free(&L.rec);
                    return 1;
                } else {
                    clear();
                    if (state->status[0]) say(&L, 1, state->status);
                }
            }
            continue;
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
