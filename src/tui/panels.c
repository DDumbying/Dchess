#include "tui/panels.h"
#include "tui/replay_tui.h"
#include "tui/puzzles_tui.h"
#include "utils/text.h"
#include "game/book.h"
#include "tui/render.h"
#include "tui/colors.h"
#include "utils/dash.h"
#include "utils/theme.h"
#include "utils/constants.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <wchar.h>

static void put_wc(WINDOW *w, int row, int col, wchar_t ch, int pair, attr_t attr)
{
    cchar_t cc;
    wchar_t s[2] = { ch, L'\0' };
    setcchar(&cc, s, attr, (short)pair, NULL);
    mvwadd_wch(w, row, col, &cc);
}

void panel_frame(WINDOW *win, const char *title, int accent_pair)
{
    int h, w;
    getmaxyx(win, h, w);
    if (h < 2 || w < 2) return;

    for (int c = 1; c < w - 1; c++) {
        put_wc(win, 0,     c, L'─', CP_BORDER, 0);
        put_wc(win, h - 1, c, L'─', CP_BORDER, 0);
    }
    for (int r = 1; r < h - 1; r++) {
        put_wc(win, r, 0,     L'│', CP_BORDER, 0);
        put_wc(win, r, w - 1, L'│', CP_BORDER, 0);
    }
    put_wc(win, 0,     0,     L'╭', CP_BORDER, 0);
    put_wc(win, 0,     w - 1, L'╮', CP_BORDER, 0);
    put_wc(win, h - 1, 0,     L'╰', CP_BORDER, 0);
    put_wc(win, h - 1, w - 1, L'╯', CP_BORDER, 0);

    if (title && w > 6) {
        wattron(win, COLOR_PAIR(accent_pair) | A_BOLD);
        mvw_clip(win, 0, 2, " %s ", title);
        wattroff(win, COLOR_PAIR(accent_pair) | A_BOLD);
    }
}

/* A sub-window over rows [row, row+h) of the side column. */
static WINDOW *sub(WINDOW *side, int row, int h)
{
    int sh, sw;
    getmaxyx(side, sh, sw);
    (void)sh;
    WINDOW *p = derwin(side, h, sw, row, 0);
    if (p) wbkgd(p, COLOR_PAIR(CP_CANVAS));
    return p;
}

static void clock_cs(const TUIState *s, long *w_cs, long *b_cs)
{
    *w_cs = game_time_spent(&s->game, WHITE) / 10;
    *b_cs = game_time_spent(&s->game, BLACK) / 10;
}

static void fmt_clock(long cs, char *out, size_t n)
{
    if (cs < 0) cs = 0;
    long m = cs / 6000, s = (cs % 6000) / 100, c = cs % 100;
    if (m > 99) { m = 99; s = 59; c = 99; }
    snprintf(out, n, "%02ld:%02ld.%02ld", m, s, c);
}

static void draw_eval_graph(WINDOW *p, int top, int left, int rows, int cols,
                            const GameState *g)
{
    static const wchar_t BLOCKS[9] = {
        L' ', L'▁', L'▂', L'▃', L'▄', L'▅', L'▆', L'▇', L'█',
    };

    int zero = top + rows - 1 - rows / 2;
    for (int c = 0; c < cols; c++)
        put_wc(p, zero, left + c, L'─', CP_HINT, 0);

    int start = dash_graph_start(g->eval_count, cols);
    for (int c = 0; c < cols && start + c < g->eval_count; c++) {
        int fill = dash_graph_fill(g->eval_history[start + c], rows);
        for (int r = 0; r < rows; r++) {          /* r = 0 is the bottom row */
            int cell = fill - r * 8;
            if (cell <= 0) continue;
            if (cell > 8) cell = 8;
            int pair = CP_RAMP_BASE + dash_ramp_index(r, rows, THEME_RAMP);
            put_wc(p, top + rows - 1 - r, left + c, BLOCKS[cell], pair, 0);
        }
    }
}

static void draw_analysis_panel(WINDOW *p, const TUIState *s)
{
    const char *name = !s->analysis_engine[0] || !strcmp(s->analysis_engine, "off") ||
                       !strcmp(s->analysis_engine, "builtin") ? "dchess" : s->analysis_engine;
    char title[80];
    snprintf(title, sizeof(title), "analysis · %s", name);
    panel_frame(p, title, CP_ACC_EVAL);
    int h, w;
    getmaxyx(p, h, w);
    const Analysis *a = &s->analysis;
    const char *note = s->analysis_err[0] ? s->analysis_err
                     : s->game.game_over ? (s->game.result[0] ? s->game.result : "game over")
                     : s->review && s->review->running && analyser_is_builtin(s->review->an) ? "reviewing…"
                     : s->analysis_blocked ? "engine thinking"
                     : !s->analysis_ready ? "analysing…" : NULL;
    if (note) {
        wattron(p, COLOR_PAIR(s->analysis_err[0] ? CP_STATUS_ERR : CP_HINT));
        mvw_fit(p, 1, 2, w - 4, note);
        wattroff(p, COLOR_PAIR(s->analysis_err[0] ? CP_STATUS_ERR : CP_HINT));
        return;
    }
    int bar = w - 4;
    double share = a->mate ? (a->mate > 0 ? 1.0 : 0.0) : 1.0 / (1.0 + pow(10.0, -a->score_cp / 400.0));
    int fill = (int)(share * bar + 0.5);
    for (int i = 0; i < bar; i++) {
        wattron(p, COLOR_PAIR(i < fill ? CP_ACC_EVAL : CP_HINT));
        mvwaddstr(p, 1, 2 + i, i < fill ? "█" : "░");
        wattroff(p, COLOR_PAIR(i < fill ? CP_ACC_EVAL : CP_HINT));
    }
    char score[24], best[32];
    if (a->mate) snprintf(score, sizeof(score), "%sM%d d%d", a->mate > 0 ? "" : "-", abs(a->mate), a->depth);
    else         snprintf(score, sizeof(score), "%+.2f d%d", a->score_cp / 100.0, a->depth);
    snprintf(best, sizeof(best), "best %s", a->line_len ? a->line[0] : "—");
    wattron(p, COLOR_PAIR(CP_ACC_EVAL) | A_BOLD);
    mvw_fit(p, 2, 2, w - 4, score);
    wattroff(p, COLOR_PAIR(CP_ACC_EVAL) | A_BOLD);
    int bw = text_width(best);
    if (bw < w - 4 - text_width(score) - 1) {
        wattron(p, COLOR_PAIR(CP_INFO_VAL));
        mvw_fit(p, 2, w - 2 - bw, bw, best);
        wattroff(p, COLOR_PAIR(CP_INFO_VAL));
    }
    /* The expected line, wrapped over the rows left. */
    int row = 3, col = 2;
    wattron(p, COLOR_PAIR(CP_HINT));
    for (int i = 0; i < a->line_len && row < h - 1; i++) {
        int len = (int)strlen(a->line[i]);
        if (col > 2 && col + len > w - 2) { row++; col = 2; }
        if (row >= h - 1) break;
        mvw_fit(p, row, col, len < w - 4 ? len : w - 4, a->line[i]);
        col += len + 1;
    }
    wattroff(p, COLOR_PAIR(CP_HINT));
}

static void draw_eval_panel(WINDOW *p, const TUIState *state)
{
    if (state->analysis_on) {
        draw_analysis_panel(p, state);
        return;
    }
    panel_frame(p, "eval", CP_ACC_EVAL);
    int h, w;
    getmaxyx(p, h, w);
    if (state->replay) {                /* no engine looks at a replay */
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, h - 2, 2, "—");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

    int rows = h - 3;   /* border top and bottom, and the value line */
    int cols = w - 4;
    if (rows >= 1 && cols >= 1)
        draw_eval_graph(p, 1, 2, rows, cols, &state->game);

    float v = 0.0f;
    sscanf(state->last_eval, "%f", &v);
    const char *who = v > 0.05f ? "white better" : v < -0.05f ? "black better" : "level";

    wattron(p, COLOR_PAIR(CP_ACC_EVAL) | A_BOLD);
    mvw_clip(p, h - 2, 2, "%s", state->last_eval);
    wattroff(p, COLOR_PAIR(CP_ACC_EVAL) | A_BOLD);
    wattron(p, COLOR_PAIR(CP_HINT));
    mvw_clip(p, h - 2, w - 2 - (int)strlen(who), "%s", who);
    wattroff(p, COLOR_PAIR(CP_HINT));
}

static void draw_bar(WINDOW *p, int row, int col, int width, int fill)
{
    for (int i = 0; i < width; i++) {
        if (i < fill) {
            int pair = CP_RAMP_BASE + dash_ramp_index(i, width, THEME_RAMP);
            put_wc(p, row, col + i, L'█', pair, 0);
        } else {
            put_wc(p, row, col + i, L' ', CP_TRACK, 0);
        }
    }
}

/* Time left: h:mm:ss, m:ss, or s.t under ten seconds. */
static void fmt_left(long ms, char *out, size_t n)
{
    if (ms < 0) ms = 0;
    long s = ms / 1000;
    if (ms < 10000)     snprintf(out, n, "%ld.%ld", s, (ms % 1000) / 100);
    else if (s >= 3600) snprintf(out, n, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
    else                snprintf(out, n, "%ld:%02ld", s / 60, s % 60);
}

static void draw_timed_clocks(WINDOW *p, const TUIState *state)
{
    const GameState *g = &state->game;
    char title[48], tc[32];
    tc_format(&g->tc, tc, sizeof(tc));
    snprintf(title, sizeof(title), "clocks · %s", tc);
    panel_frame(p, title, CP_ACC_CLOCK);
    int h, w;
    getmaxyx(p, h, w);
    (void)h;
    for (int side = WHITE; side <= BLACK; side++) {
        long left = game_time_left(g, side);
        char t[24];
        fmt_left(left, t, sizeof(t));
        int row = side == WHITE ? 1 : 3;
        int active = !g->game_over && g->clock_side == side;
        attr_t a = left < 10000 ? (COLOR_PAIR(CP_STATUS_ERR) | A_BOLD)
                 : active ? (COLOR_PAIR(CP_ACC_CLOCK) | A_BOLD) : COLOR_PAIR(CP_INFO_VAL);
        wattron(p, a);
        mvw_clip(p, row, 2, "%s", side == WHITE ? "W" : "B");
        mvw_clip(p, row, w - 2 - (int)strlen(t), "%s", t);
        wattroff(p, a);
        int bar_w = w - 4;
        draw_bar(p, row + 1, 2, bar_w, dash_bar_fill(left > 0 ? left : 0, g->tc.base_ms[side], bar_w));
    }
}

static void draw_clock_panel(WINDOW *p, const TUIState *state)
{
    if (tc_timed(&state->game.tc) && !state->replay) {
        draw_timed_clocks(p, state);
        return;
    }
    panel_frame(p, "clocks", CP_ACC_CLOCK);
    int h, w;
    getmaxyx(p, h, w);
    (void)h;
    if (state->replay) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, "W");
        mvw_clip(p, 1, w - 3, "—");
        mvw_clip(p, 3, 2, "B");
        mvw_clip(p, 3, w - 3, "—");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

    long w_cs, b_cs;
    clock_cs(state, &w_cs, &b_cs);

    const struct { const char *label; long cs; int side; int row; } rows[2] = {
        { "W", w_cs, WHITE, 1 }, { "B", b_cs, BLACK, 3 },
    };
    for (int i = 0; i < 2; i++) {
        char t[16];
        fmt_clock(rows[i].cs, t, sizeof(t));
        int active = !state->game.game_over && state->game.clock_side == rows[i].side;
        attr_t a = active ? (COLOR_PAIR(CP_ACC_CLOCK) | A_BOLD) : COLOR_PAIR(CP_INFO_VAL);
        wattron(p, a);
        mvw_clip(p, rows[i].row, 2, "%s", rows[i].label);
        mvw_clip(p, rows[i].row, w - 2 - (int)strlen(t), "%s", t);
        wattroff(p, a);
    }

    long total = w_cs + b_cs;
    int  bar_w = w - 4;
    draw_bar(p, 2, 2, bar_w, dash_bar_fill(w_cs, total, bar_w));
    draw_bar(p, 4, 2, bar_w, dash_bar_fill(b_cs, total, bar_w));
}

static void draw_moves_panel(WINDOW *p, const TUIState *state)
{
    panel_frame(p, "moves", CP_ACC_MOVES);
    int h, w;
    getmaxyx(p, h, w);
    (void)w;

    const GameState *g = &state->game;
    const ReplayGame *rp = state->replay;
    /* A replay lists the whole game; the moves after the current one dim. */
    int count = rp ? g->log_start + rp->count : g->move_count;
    int cur   = g->move_count - 1;
    int total = (count + 1) / 2;
    int from  = g->log_start / 2;           /* a FEN may start mid-game */
    if (count <= g->log_start) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, "no moves yet");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

    const char *eco = NULL, *opening = book_opening(g, &eco);
    int rows  = h - 2 - (opening && h > 5);
    int first = total - from > rows ? total - rows : from;
    if (rp && total - from > rows) {
        first = cur / 2 - rows / 2;
        if (first > total - rows) first = total - rows;
        if (first < from) first = from;
    }
    if (opening && h > 5) {
        wattron(p, COLOR_PAIR(CP_ACC_MOVES));
        mvw_clip(p, h - 2, 2, "%s %s", eco, opening);
        wattroff(p, COLOR_PAIR(CP_ACC_MOVES));
    }
    for (int m = first; m < total; m++) {
        int r = 1 + (m - first);
        int wi = 2 * m, bi = 2 * m + 1;

        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, r, 1, "%3d.", m + 1);
        wattroff(p, COLOR_PAIR(CP_HINT));

        for (int k = 0; k < 2; k++) {
            int i = k ? bi : wi;
            if (i >= count) break;
            attr_t a = i == cur ? (COLOR_PAIR(CP_ACC_MOVES) | A_BOLD)
                     : i > cur  ? COLOR_PAIR(CP_HINT) : COLOR_PAIR(CP_INFO_VAL);
            const char *san = i < g->log_start ? "..."
                            : rp ? rp->san[i - g->log_start] : g->move_history[i];
            char marked[16];
            Grade gr = rp && i >= g->log_start ? replay_grade(state, i - g->log_start, NULL) : GRADE_NONE;
            snprintf(marked, sizeof(marked), "%s%s", san, grade_mark(gr));
            wattron(p, a);
            mvw_clip(p, r, k ? 14 : 6, "%s", marked);
            wattroff(p, a);
        }
    }
}

static void engine_row(WINDOW *p, int row, const char *label, const char *value)
{
    int h, w;
    getmaxyx(p, h, w);
    (void)h;
    wattron(p, COLOR_PAIR(CP_HINT));
    mvw_clip(p, row, 2, "%s", label);
    wattroff(p, COLOR_PAIR(CP_HINT));
    wattron(p, COLOR_PAIR(CP_INFO_VAL));
    mvw_clip(p, row, w - 2 - (int)strlen(value), "%s", value);
    wattroff(p, COLOR_PAIR(CP_INFO_VAL));
}

static void draw_engine_panel(WINDOW *p, const TUIState *state)
{
    const char *by = state->thinking ? state->thinking_by : state->last_search_by;
    char title[64];
    if (by[0]) snprintf(title, sizeof(title), "engine · %s", by);
    else       snprintf(title, sizeof(title), "engine");
    panel_frame(p, title, CP_ACC_ENGINE);
    if (state->replay) {
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, "replay");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

    if (state->thinking) {
        wattron(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        mvw_clip(p, 1, 2, "thinking...");
        wattroff(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        return;
    }
    if (state->engine_error[0]) {
        wattron(p, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        mvw_clip(p, 1, 2, "failed");
        wattroff(p, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        return;
    }
    if (state->paused && players_automated(state->players, state->game.pos.side)) {
        wattron(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        mvw_clip(p, 1, 2, "paused");
        wattroff(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        return;
    }

    if (state->last_was_book) {
        wattron(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        mvw_clip(p, 1, 2, "book");
        wattroff(p, COLOR_PAIR(CP_ACC_ENGINE) | A_BOLD);
        return;
    }
    const SearchResult *r = &state->last_search;
    if (r->nodes == 0) {
        int any = players_automated(state->players, WHITE) ||
                  players_automated(state->players, BLACK);
        wattron(p, COLOR_PAIR(CP_HINT));
        mvw_clip(p, 1, 2, any ? "waiting" : "no engine");
        wattroff(p, COLOR_PAIR(CP_HINT));
        return;
    }

    char depth[16], nodes[16], nps[16];
    snprintf(depth, sizeof(depth), "%d", r->depth_reached);
    dash_count(r->nodes, nodes, sizeof(nodes));
    long ms = r->elapsed_ms > 0 ? r->elapsed_ms : 1;
    dash_count(r->nodes * 1000 / ms, nps, sizeof(nps));

    engine_row(p, 1, "depth", depth);
    engine_row(p, 2, "nodes", nodes);
    engine_row(p, 3, "nps",   nps);
}

static void draw_puzzle_panel(WINDOW *p, const TUIState *state)
{
    const PuzzleView *v = state->puzzle;
    static const char *const modes[] = { "Rated", "Themes", "Rush", "Missed" };
    panel_frame(p, "puzzle", CP_ACC_BOARD);
    int h, w, row = 1;
    getmaxyx(p, h, w);
    char line[128];
    if (v->mode == PM_THEMES) snprintf(line, sizeof(line), "Themes · %s", puzzle_theme_name((int)v->theme));
    else if (v->mode == PM_MISSED) snprintf(line, sizeof(line), "Missed · %d to go", v->left);
    else snprintf(line, sizeof(line), "%s", modes[v->mode]);
    wattron(p, COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
    mvw_fit(p, row++, 2, w - 4, line);
    wattroff(p, COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);

    static const char *const says[] = { "Find the best move", "Right — keep going", "Wrong",
                                        "Solved", "Solution shown", "Run over" };
    int good = v->verdict == PV_RIGHT || v->verdict == PV_SOLVED;
    int bad = v->verdict == PV_WRONG || v->verdict == PV_OVER;
    attr_t a = good ? COLOR_PAIR(CP_STATUS_OK) | A_BOLD : bad ? COLOR_PAIR(CP_STATUS_ERR) | A_BOLD
                    : COLOR_PAIR(CP_INFO_VAL);
    snprintf(line, sizeof(line), "You play %s", state->view_side == WHITE ? "White" : "Black");
    mvw_fit(p, row++, 2, w - 4, line);
    wattron(p, a);
    mvw_fit(p, row++, 2, w - 4, says[v->verdict]);
    wattroff(p, a);
    row++;

    if (v->mode == PM_RUSH) {
        long s = (v->rush_left_ms + 999) / 1000;
        snprintf(line, sizeof(line), "Solved %d   Mistakes %d/3   %ld:%02ld", v->score, v->strikes, s / 60, s % 60);
    } else if (v->mode == PM_RATED && v->rated) {
        snprintf(line, sizeof(line), "Rating %d (%+d)   Streak %d", v->rating, v->delta, v->streak);
    } else if (v->mode == PM_RATED) {
        snprintf(line, sizeof(line), "Rating %d   Streak %d", v->rating, v->streak);
    } else {
        snprintf(line, sizeof(line), "Unrated");
    }
    mvw_fit(p, row++, 2, w - 4, line);
    row++;

    if (v->pz.done || v->pz.failed) {   /* what it was, once it can no longer help */
        const PuzzleData *d = &puzzle_data[v->pz.index];
        snprintf(line, sizeof(line), "Puzzle rated %d", d->rating);
        mvw_fit(p, row++, 2, w - 4, line);
        line[0] = '\0';
        for (int b = 0; b < TH_BITS; b++)
            if (d->themes & (1 << b)) {
                size_t n = strlen(line);
                snprintf(line + n, sizeof(line) - n, "%s%s", n ? ", " : "", puzzle_theme_name(1 << b));
            }
        wattron(p, COLOR_PAIR(CP_HINT));
        if (line[0] && row < h - 2) mvw_fit(p, row++, 2, w - 4, line);
        snprintf(line, sizeof(line), "lichess.org/training/%s", d->id);
        if (row < h - 1) mvw_fit(p, row++, 2, w - 4, line);
        wattroff(p, COLOR_PAIR(CP_HINT));
    }
}

void draw_side_column(WINDOW *side, const TUIState *state)
{
    if (!side) return;
    werase(side);

    int h, w;
    getmaxyx(side, h, w);
    (void)w;
    if (state->puzzle) {
        WINDOW *p = sub(side, 0, h);
        if (p) {
            draw_puzzle_panel(p, state);
            delwin(p);
        }
        wnoutrefresh(side);
        return;
    }
    DashSide L = dash_side_layout(h);

    struct { int h; void (*draw)(WINDOW *, const TUIState *); } panes[4] = {
        { L.eval_h,   draw_eval_panel   },
        { L.clock_h,  draw_clock_panel  },
        { L.moves_h,  draw_moves_panel  },
        { L.engine_h, draw_engine_panel },
    };

    int row = 0;
    for (int i = 0; i < 4; i++) {
        if (panes[i].h <= 0) continue;
        WINDOW *p = sub(side, row, panes[i].h);
        if (p) {
            panes[i].draw(p, state);
            delwin(p);
        }
        row += panes[i].h;
    }
    wnoutrefresh(side);
}

void draw_command_bar(WINDOW *cmd, const TUIState *state)
{
    werase(cmd);
    panel_frame(cmd, "command", CP_ACC_BOARD);
    int h, w;
    getmaxyx(cmd, h, w);
    (void)h;

    if (state->replay) {
        const ReplayGame *rp = state->replay;
        char head[320];
        char extra[64] = "";
        int loss = 0, j = state->replay_ply - 1;
        Grade gr = replay_grade(state, j, &loss);
        if (state->review && state->review->running)
            snprintf(extra, sizeof(extra), " · reviewing %d/%d", state->review->next, rp->count + 1);
        else if (gr != GRADE_NONE)
            snprintf(extra, sizeof(extra), " · %s%s %+.2f", rp->san[j], grade_mark(gr), -loss / 100.0);
        snprintf(head, sizeof(head), "REPLAY · %d/%d · %s – %s · %s%s", state->replay_ply, rp->count,
                 rp->info.white[0] ? rp->info.white : "?", rp->info.black[0] ? rp->info.black : "?",
                 rp->info.result[0] ? rp->info.result : "*", extra);
        wattron(cmd, COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
        mvw_fit(cmd, 1, 2, w - 4, head);
        wattroff(cmd, COLOR_PAIR(CP_ACC_BOARD) | A_BOLD);
        const char *keys = state->replay_auto ? "space pause  esc back"
                         : "←→ step  space auto  a analyse  r review  n/N marks  p play  esc back";
        int kw = text_width(keys), sw = w - 4 - kw - 2;
        if (state->status[0] && text_width(state->status) > sw) {   /* no room for both: the message wins */
            wattron(cmd, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
            mvw_fit(cmd, 2, 2, w - 4, state->status);
            wattroff(cmd, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
            wnoutrefresh(cmd);
            return;
        }
        if (state->status[0]) {
            wattron(cmd, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
            mvw_fit(cmd, 2, 2, sw, state->status);
            wattroff(cmd, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        }
        wattron(cmd, COLOR_PAIR(CP_HINT));
        mvw_fit(cmd, 2, kw < w - 4 ? w - 2 - kw : 2, kw < w - 4 ? kw : w - 4, keys);
        wattroff(cmd, COLOR_PAIR(CP_HINT));
        wnoutrefresh(cmd);
        return;
    }
    if (state->puzzle) {
        const char *keys = "i type  ⏎ move  ? hint  s show  n next  r retry  esc back";
        int kw = text_width(keys), room = w - 4;
        int bad = strncmp(state->status, "Wrong", 5) == 0 || strncmp(state->status, "Not a", 5) == 0;
        attr_t a = bad ? COLOR_PAIR(CP_STATUS_ERR) | A_BOLD : COLOR_PAIR(CP_STATUS_OK) | A_BOLD;
        int sw = kw + 2 < room ? room - kw - 2 : room;
        wattron(cmd, a);
        mvw_fit(cmd, 2, 2, sw, state->status);
        wattroff(cmd, a);
        if (kw + 2 < room && text_width(state->status) <= sw) {
            wattron(cmd, COLOR_PAIR(CP_HINT));
            mvw_fit(cmd, 2, w - 2 - kw, kw, keys);
            wattroff(cmd, COLOR_PAIR(CP_HINT));
        }
        wnoutrefresh(cmd);
        return;
    }
    int is_err = strncmp(state->status, "Illegal", 7) == 0 ||
                 strncmp(state->status, "Bad",     3) == 0 ||
                 strncmp(state->status, "Unknown", 7) == 0;
    const char *hints = "i type  u undo  tab stats";
    int room = w - 4;
    int show_hints = room > (int)strlen(state->status) + (int)strlen(hints) + 3;

    attr_t a = is_err ? (COLOR_PAIR(CP_STATUS_ERR) | A_BOLD)
                      : (COLOR_PAIR(CP_STATUS_OK)  | A_BOLD);
    wattron(cmd, a);
    mvw_clip(cmd, 2, 2, "%s", state->status);
    wattroff(cmd, a);

    if (show_hints) {
        wattron(cmd, COLOR_PAIR(CP_HINT));
        mvw_clip(cmd, 2, w - 2 - (int)strlen(hints), "%s", hints);
        wattroff(cmd, COLOR_PAIR(CP_HINT));
    }
    wnoutrefresh(cmd);
}
