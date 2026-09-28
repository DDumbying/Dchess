#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tui/puzzles_tui.h"
#include "tui/render.h"
#include "tui/input.h"
#include "tui/colors.h"
#include "game/puzzle_stats.h"
#include "game/replay.h"
#include "engine/move.h"

#define RUSH_MS 180000
#define RUSH_START 800
#define RUSH_PAUSE_MS 700

typedef struct {
    TUIState    *s;
    PuzzleStats  st;
    const char  *who;           /* NULL: a guest */
    int          saves;         /* a profile whose name makes a file */
    char         dir[512];
    unsigned     seed;
} Session;

static void save(Session *ss)
{
    if (ss->saves) puzzle_stats_save(ss->dir, ss->who, &ss->st);
}

static void begin(Session *ss, PuzzleView *v, int index)
{
    TUIState *s = ss->s;
    puzzle_start(&v->pz, index, &s->game);
    v->verdict = PV_SOLVING;
    v->rated = 0;
    v->hint_sq = v->wrong_from = v->wrong_to = -1;
    s->view_side = s->game.pos.side;
    s->cursor_row = 6;
    s->cursor_col = 4;
    s->selected = 0;
    memset(s->highlight, 0, sizeof(s->highlight));
    snprintf(s->status, sizeof(s->status), "%s to move — find the best move",
             s->game.pos.side == WHITE ? "White" : "Black");
}

/* A rated result counts once: the first solve, fail, hint, show or skip. */
static void rate(Session *ss, PuzzleView *v, int solved)
{
    if (v->mode != PM_RATED || v->rated) return;
    int before = ss->st.rating;
    puzzle_stats_record(&ss->st, v->pz.index, solved);
    v->rated = 1;
    v->delta = ss->st.rating - before;
    v->rating = ss->st.rating;
    v->streak = ss->st.streak;
    save(ss);
}

/* Themes and Rush keep their own seen lists, for this visit only. */
static unsigned char visit_seen[PUZZLE_MAX];

static int next_index(Session *ss, PuzzleView *v)
{
    switch (v->mode) {
    case PM_RATED: {
        int i = puzzle_stats_next_rated(&ss->st, &ss->seed);
        save(ss);   /* an unfinished one comes back next time */
        return i;
    }
    case PM_THEMES: return puzzle_pick(visit_seen, ss->st.rating, v->theme, &ss->seed);
    case PM_RUSH:   return puzzle_pick(visit_seen, RUSH_START + 100 * v->score, 0, &ss->seed);
    case PM_MISSED:
        v->left = ss->st.nmissed;
        if (!ss->st.nmissed) return -1;
        v->missed_at %= ss->st.nmissed;
        return ss->st.missed[v->missed_at];
    }
    return -1;
}

static void rush_over(Session *ss, PuzzleView *v, const char *why)
{
    v->verdict = PV_OVER;
    v->pz.done = 1;
    if (v->score > ss->st.rush_best) {
        ss->st.rush_best = v->score;
        save(ss);
    }
    snprintf(ss->s->status, sizeof(ss->s->status), "%s — %d solved (best %d)", why, v->score, ss->st.rush_best);
}

static void judge(Session *ss, PuzzleView *v, Move m)
{
    TUIState *s = ss->s;
    PuzzleVerdict r = puzzle_try(&v->pz, &s->game, m);
    v->hint_sq = v->wrong_from = v->wrong_to = -1;
    if (r == PZ_WRONG) {
        v->verdict = PV_WRONG;
        v->wrong_from = FROM(m);
        v->wrong_to = TO(m);
        if (v->mode == PM_RUSH) {
            if (++v->strikes >= 3) { rush_over(ss, v, "Three mistakes"); return; }
            snprintf(s->status, sizeof(s->status), "Wrong — %d of 3 mistakes", v->strikes);
            v->advance_at = game_now_ms() + RUSH_PAUSE_MS;
            return;
        }
        rate(ss, v, 0);
        snprintf(s->status, sizeof(s->status), "Wrong — try again, or n for the next one");
        return;
    }
    if (r == PZ_RIGHT) {
        v->verdict = PV_RIGHT;
        snprintf(s->status, sizeof(s->status), "Right — keep going");
        return;
    }
    v->verdict = PV_SOLVED;
    if (v->mode == PM_RUSH) {
        v->score++;
        snprintf(s->status, sizeof(s->status), "Solved — %d so far", v->score);
        v->advance_at = game_now_ms() + RUSH_PAUSE_MS;
        return;
    }
    rate(ss, v, !v->pz.failed);
    if (v->mode == PM_MISSED && !v->pz.failed) {
        puzzle_stats_forgive(&ss->st, v->pz.index);
        v->left = ss->st.nmissed;
        save(ss);
    }
    snprintf(s->status, sizeof(s->status), "%s — n for the next one",
             v->pz.failed ? "Solved, after a slip" : "Solved!");
}

/* A typed move: SAN, else coordinates. */
static int typed_move(const GameState *g, const char *text, Move *out)
{
    int from, to, promo;
    Move m = replay_find_san(g, text);
    if (m) { *out = m; return 1; }
    return parse_move_str(text, &from, &to, &promo) && game_find_move(g, from, to, promo, out);
}

/* One mode, puzzle after puzzle, until esc (or a finished Rush and esc). */
static void run_mode(Session *ss, PuzzleMode mode, unsigned theme)
{
    TUIState *s = ss->s;
    PuzzleView v;
    memset(&v, 0, sizeof(v));
    v.mode = mode;
    v.theme = theme;
    v.rating = ss->st.rating;
    v.streak = ss->st.streak;
    memset(visit_seen, 0, sizeof(visit_seen));
    int first = next_index(ss, &v);
    if (first < 0) return;
    s->puzzle = &v;
    begin(ss, &v, first);
    long rush_end = game_now_ms() + RUSH_MS;
    v.rush_left_ms = RUSH_MS;

    Screen *sc = tui_screen_open(s);
    if (!sc) { s->puzzle = NULL; return; }
    char cmd[256];
    for (;;) {
        if (mode == PM_RUSH && v.verdict != PV_OVER) {
            v.rush_left_ms = rush_end - game_now_ms();
            if (v.rush_left_ms <= 0) { v.rush_left_ms = 0; v.advance_at = 0; rush_over(ss, &v, "Time"); }
        }
        if (v.advance_at && game_now_ms() >= v.advance_at) {   /* the verdict has been seen */
            v.advance_at = 0;
            int i = next_index(ss, &v);
            if (i >= 0) begin(ss, &v, i);
        }
        WINDOW *in = tui_screen_input(sc);   /* a resize replaces the window */
        tui_screen_paint(sc);
        wtimeout(in, mode == PM_RUSH && v.verdict != PV_OVER ? 100 : -1);
        int typing = s->insert_mode;
        int ch = read_key(in, cmd, sizeof(cmd), &s->insert_mode);
        if (ch == 0) continue;
        if (ch == KEY_RESIZE) { tui_screen_resize(sc); continue; }
        if (ch == 27 && typing) continue;   /* Esc only cancelled the typing */
        if (ch == 27) {
            if (s->selected) { s->selected = 0; memset(s->highlight, 0, sizeof(s->highlight)); continue; }
            if (mode == PM_RUSH && v.verdict != PV_OVER) rush_over(ss, &v, "Stopped");   /* keeps a new best */
            break;
        }
        if (v.advance_at) continue;   /* the next Rush puzzle is on its way */
        Move m;
        if (ch == -2) {
            if (v.pz.done) continue;
            if (typed_move(&s->game, cmd, &m)) judge(ss, &v, m);
            else snprintf(s->status, sizeof(s->status), "Not a legal move: %.40s", cmd);
            continue;
        }
        int picked = v.pz.done && (ch == '\n') ? 0 : tui_cursor_key(s, ch, &m);
        if (picked == 1) { judge(ss, &v, m); continue; }
        if (picked == 0) continue;
        switch (ch) {
        case '?':
            if (v.pz.done) break;
            if (mode == PM_RUSH) { rush_over(ss, &v, "Hint taken"); break; }
            v.hint_sq = puzzle_hint(&v.pz);
            rate(ss, &v, 0);
            snprintf(s->status, sizeof(s->status), "Hint: the piece on %c%d", 'a' + v.hint_sq % 8, v.hint_sq / 8 + 1);
            break;
        case 's':
            if (v.pz.done) break;
            if (mode == PM_RUSH) { rush_over(ss, &v, "Solution shown"); break; }
            rate(ss, &v, 0);
            while (puzzle_show_step(&v.pz, &s->game)) {}
            v.verdict = PV_SHOWN;
            v.hint_sq = v.wrong_from = v.wrong_to = -1;
            snprintf(s->status, sizeof(s->status), "The solution — n for the next one");
            break;
        case 'n': {
            if (mode == PM_RUSH) break;   /* no free skips */
            if (!v.pz.done) rate(ss, &v, 0);   /* skipping a rated puzzle counts as a miss */
            if (mode == PM_MISSED &&
                v.missed_at < ss->st.nmissed && ss->st.missed[v.missed_at] == v.pz.index)
                v.missed_at++;                 /* still on the list: move past it */
            int i = next_index(ss, &v);
            if (i < 0) { snprintf(s->status, sizeof(s->status), "No more missed puzzles"); break; }
            begin(ss, &v, i);
            break;
        }
        case 'r': {
            if (mode == PM_RUSH) break;
            int counted = v.rated;   /* a retry never counts again */
            begin(ss, &v, v.pz.index);
            v.rated = counted;
            break;
        }
        default: break;
        }
    }
    wtimeout(tui_screen_input(sc), -1);
    tui_screen_close(sc);
    s->puzzle = NULL;
    s->status[0] = '\0';
}

static void draw_menu(const Session *ss, const char *const *items, int n, int sel, int top, const char *title,
                      const char *msg)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();
    char line[256];
    snprintf(line, sizeof(line), "dchess · puzzles · %s", title);
    attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvw_fit(stdscr, 0, 1, cols - 2, line);
    attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);
    char who[80];
    if (!ss->who) snprintf(who, sizeof(who), "guest (not saved)");
    else snprintf(who, sizeof(who), "%.60s%s", ss->who, ss->saves ? "" : " (not saved)");
    snprintf(line, sizeof(line), "%s · rating %d · streak %d (best %d) · rush best %d · %d missed",
             who, ss->st.rating, ss->st.streak, ss->st.best_streak,
             ss->st.rush_best, ss->st.nmissed);
    attron(COLOR_PAIR(CP_HINT));
    mvw_fit(stdscr, 1, 1, cols - 2, line);
    attroff(COLOR_PAIR(CP_HINT));
    for (int r = 0; top + r < n && 3 + r < rows - 2; r++) {
        int i = top + r;
        if (i == sel) attron(A_REVERSE);
        mvw_fit(stdscr, 3 + r, 3, cols - 6, items[i]);
        if (i == sel) attroff(A_REVERSE);
    }
    if (msg && msg[0]) {
        attron(COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        mvw_fit(stdscr, rows - 2, 1, cols - 2, msg);
        attroff(COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
    }
    attron(COLOR_PAIR(CP_HINT));
    mvw_fit(stdscr, rows - 1, 1, cols - 2, "↑↓ move  ⏎ choose  esc back");
    attroff(COLOR_PAIR(CP_HINT));
    refresh();
}

/* ↑↓ and ⏎ over `items`; the chosen index, or -1 on esc. */
static int choose(const Session *ss, const char *const *items, int n, int *sel, const char *title,
                  const char *msg)
{
    keypad(stdscr, TRUE);
    int top = 0;
    for (;;) {
        int page = getmaxy(stdscr) - 5;   /* the title rows, the message and the keys */
        if (page < 1) page = 1;
        if (*sel < top) top = *sel;
        if (*sel >= top + page) top = *sel - page + 1;
        draw_menu(ss, items, n, *sel, top, title, msg);
        int ch = getch();
        switch (ch) {
        case KEY_UP:   case 'k': *sel = (*sel + n - 1) % n; break;
        case KEY_DOWN: case 'j': *sel = (*sel + 1) % n; break;
        case 27: return -1;
        case '\n': case '\r': case KEY_ENTER: return *sel;
        default: break;
        }
        msg = NULL;
    }
}

void puzzles_screen(TUIState *s)
{
    static Session ss;
    memset(&ss, 0, sizeof(ss));
    ss.s = s;
    ss.seed = (unsigned)time(NULL) ^ (unsigned)getpid();
    ss.who = s->profiles.count && s->first_run != 2 ? s->profiles.p[s->profiles.active].name : NULL;
    ss.saves = ss.who && puzzle_stats_can_save(ss.who);
    puzzle_stats_dir(ss.dir, sizeof(ss.dir));
    if (ss.saves) puzzle_stats_load(ss.dir, ss.who, &ss.st);
    else puzzle_stats_init(&ss.st);

    int sel = 0, theme_sel = 0;
    const char *msg = NULL;
    for (;;) {
        char missed[48];
        snprintf(missed, sizeof(missed), "Missed (%d)      retry the ones you got wrong", ss.st.nmissed);
        const char *items[] = {
            "Rated           puzzles near your rating",
            "Themes          practise one kind, unrated",
            "Rush            3 minutes, getting harder; 3 mistakes end it",
            missed,
        };
        int c = choose(&ss, items, 4, &sel, "choose a mode", msg);
        msg = NULL;
        if (c < 0) break;
        if (c == PM_THEMES) {
            const char *names[TH_BITS];
            for (int i = 0; i < TH_BITS; i++) names[i] = puzzle_theme_name(1 << i);
            int t = choose(&ss, names, TH_BITS, &theme_sel, "choose a theme", NULL);
            if (t >= 0) run_mode(&ss, PM_THEMES, 1u << t);
        } else if (c == PM_MISSED && !ss.st.nmissed) {
            msg = "Nothing missed yet";
        } else {
            run_mode(&ss, (PuzzleMode)c, 0);
        }
        clear();
    }
    clear();
}
