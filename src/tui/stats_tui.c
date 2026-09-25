/* stats_tui.c — ncurses statistics overlay for dchess
 * draw_stats_overlay()  — full stats screen with rolling win-rate graph
 * show_stats_overlay()  — blocking wrapper used by the standalone --stats TUI
 */

#include "tui/stats_tui.h"
#include "tui/colors.h"
#include "tui/render.h"
#include "tui/panels.h"

/* The statistics screens draw with the active theme's pairs. */
#define SCP_BORDER   CP_BORDER
#define SCP_TITLE    CP_ACC_BOARD
#define SCP_HEAD     CP_ACC_ENGINE
#define SCP_BAR_WIN  CP_STATUS_OK
#define SCP_BAR_LOSS CP_STATUS_ERR
#define SCP_BAR_DRAW CP_ACC_CLOCK
#define SCP_BAR_BG   CP_TRACK
#define SCP_VAL      CP_INFO_VAL
#define SCP_HINT     CP_HINT
#define SCP_LABEL    CP_INFO_VAL
#define SCP_GOOD     CP_STATUS_OK
#define SCP_BAD      CP_STATUS_ERR
#define SCP_NEUT     CP_ACC_CLOCK
#define SCP_GRAPH_AX CP_HINT
#define SCP_GRAPH_W  CP_STATUS_OK
#define SCP_GRAPH_L  CP_STATUS_ERR
#define SCP_GRAPH_D  CP_ACC_CLOCK
#define SCP_GRAPH_BG CP_TRACK
#include "tui/panel.h"
#include "utils/stats.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


/* Shared drawing helpers  */

static void draw_bar(WINDOW *win, int row, int col,
                     int filled, int total,
                     attr_t filled_attr, attr_t bg_attr)
{
    for (int i = 0; i < total; i++) {
        if (i < filled) {
            wattron(win, filled_attr);
            mvwaddch(win, row, col + i, ACS_BLOCK);
            wattroff(win, filled_attr);
        } else {
            wattron(win, bg_attr);
            mvwaddch(win, row, col + i, ACS_BULLET);
            wattroff(win, bg_attr);
        }
    }
}

static void draw_section_head(WINDOW *win, int row, int col, int width,
                               const char *title)
{
    wattron(win, COLOR_PAIR(SCP_HEAD) | A_BOLD);
    mvwprintw(win, row, col, "%s", title);
    int tlen = (int)strlen(title) + 1;
    wattron(win, COLOR_PAIR(SCP_HEAD));
    for (int i = col + tlen; i < col + width; i++)
        mvwaddch(win, row, i, ACS_HLINE);
    wattroff(win, COLOR_PAIR(SCP_HEAD));
}

/* Win-rate history graph  */

#define GRAPH_WIN_SIZE 10

static void draw_history_graph(WINDOW *win, int row_top, int col_l,
                                int height, int width,
                                const DchessStats *s)
{
    if (height < 8 || width < 24) return;

    draw_section_head(win, row_top, col_l, width, "WIN RATE HISTORY");

    int plot_top    = row_top + 2;
    int plot_bottom = row_top + height - 4;  /* leave room for x-axis + label */
    int plot_height = plot_bottom - plot_top + 1;
    int plot_left   = col_l + 6;   /* room for Y labels */
    int plot_right  = col_l + width - 6;
    int plot_width  = plot_right - plot_left;

    if (plot_height < 3 || plot_width < 8) return;

    int hc = s->history_count;

    /* Y-axis labels */
    wattron(win, COLOR_PAIR(SCP_GRAPH_AX));
    mvwprintw(win, plot_top,                      col_l, " 100%%");
    mvwprintw(win, plot_top + plot_height / 2,    col_l, "  50%%");
    mvwprintw(win, plot_bottom,                   col_l, "   0%%");
    wattroff(win, COLOR_PAIR(SCP_GRAPH_AX));

    /* 50% guide dashes */
    int mid_row = plot_top + plot_height / 2;
    wattron(win, COLOR_PAIR(SCP_GRAPH_AX) | A_DIM);
    for (int c = plot_left; c <= plot_right; c++)
        mvwaddch(win, mid_row, c, (c % 2 == 0) ? '-' : ' ');
    wattroff(win, COLOR_PAIR(SCP_GRAPH_AX) | A_DIM);

    /* X-axis line */
    wattron(win, COLOR_PAIR(SCP_GRAPH_AX));
    mvwaddch(win, plot_bottom + 1, plot_left - 1, ACS_LLCORNER);
    for (int c = plot_left; c <= plot_right; c++)
        mvwaddch(win, plot_bottom + 1, c, ACS_HLINE);
    wattroff(win, COLOR_PAIR(SCP_GRAPH_AX));

    if (hc == 0) {
        wattron(win, COLOR_PAIR(SCP_HINT) | A_BOLD);
        const char *msg = "Play more games to build your history graph";
        int mx = plot_left + (plot_width - (int)strlen(msg)) / 2;
        if (mx < plot_left) mx = plot_left;
        mvwprintw(win, plot_top + plot_height / 2, mx, "%s", msg);
        wattroff(win, COLOR_PAIR(SCP_HINT) | A_BOLD);

        /* Legend even when empty */
        int leg_row = plot_bottom + 2;
        wattron(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
        mvwprintw(win, leg_row, plot_left, "Win rate");
        wattroff(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
        wattron(win, COLOR_PAIR(SCP_GRAPH_L));
        mvwprintw(win, leg_row, plot_left + 12, "Loss rate");
        wattroff(win, COLOR_PAIR(SCP_GRAPH_L));
        wattron(win, COLOR_PAIR(SCP_HINT) | A_DIM);
        mvwprintw(win, leg_row, plot_left + 25,
                  "(rolling %d-game window)", GRAPH_WIN_SIZE);
        wattroff(win, COLOR_PAIR(SCP_HINT) | A_DIM);
        return;
    }

    /* X-axis time labels */
    {
        wattron(win, COLOR_PAIR(SCP_GRAPH_AX));
        time_t t0 = (time_t)s->history[0].timestamp;
        struct tm *tm0 = localtime(&t0);
        char buf0[16];
        strftime(buf0, sizeof(buf0), "%b %d", tm0);
        mvwprintw(win, plot_bottom + 2, plot_left, "%s", buf0);

        time_t t1 = (time_t)s->history[hc - 1].timestamp;
        struct tm *tm1 = localtime(&t1);
        char buf1[16];
        strftime(buf1, sizeof(buf1), "%b %d", tm1);
        int lx = plot_right - (int)strlen(buf1) + 1;
        if (lx > plot_left + (int)strlen(buf0) + 2)
            mvwprintw(win, plot_bottom + 2, lx, "%s", buf1);
        wattroff(win, COLOR_PAIR(SCP_GRAPH_AX));
    }

    /* Plot rolling win-rate and loss-rate lines */
    int prev_wr_row = -1;
    int prev_lo_row = -1;

    for (int cx = 0; cx < plot_width; cx++) {
        /* Map column → game index */
        int gi = (hc <= plot_width)
                    ? cx * hc / plot_width
                    : (hc - plot_width) + cx;
        if (gi < 0)   gi = 0;
        if (gi >= hc) gi = hc - 1;

        /* Sliding window */
        int w_start = gi - GRAPH_WIN_SIZE / 2;
        int w_end   = gi + GRAPH_WIN_SIZE / 2;
        if (w_start < 0)   w_end  -= w_start, w_start = 0;
        if (w_end   >= hc) w_end   = hc - 1;

        int ww = 0, wl = 0, wd = 0;
        for (int k = w_start; k <= w_end; k++) {
            if      (s->history[k].result ==  1) ww++;
            else if (s->history[k].result == -1) wl++;
            else                                  wd++;
        }
        int total_w = ww + wl + wd;
        if (total_w == 0) continue;

        float win_rate  = (float)ww / total_w;
        float loss_rate = (float)wl / total_w;

        int wr_row = plot_bottom - (int)(win_rate  * (plot_height - 1));
        int lo_row = plot_bottom - (int)(loss_rate * (plot_height - 1));
        if (wr_row < plot_top)    wr_row = plot_top;
        if (lo_row < plot_top)    lo_row = plot_top;
        if (wr_row > plot_bottom) wr_row = plot_bottom;
        if (lo_row > plot_bottom) lo_row = plot_bottom;

        int screen_col = plot_left + cx;

        /* Win-rate line — draw vertical segment from prev to current */
        {
            int r1, r2;
            if (prev_wr_row < 0) { r1 = wr_row; r2 = wr_row; }
            else if (prev_wr_row < wr_row) { r1 = prev_wr_row; r2 = wr_row; }
            else                           { r1 = wr_row; r2 = prev_wr_row; }
            for (int r = r1; r <= r2; r++) {
                wattron(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
                mvwaddch(win, r, screen_col, ACS_BLOCK);
                wattroff(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
            }
        }
        prev_wr_row = wr_row;

        /* Loss-rate line — dots, skip rows occupied by win line */
        if (lo_row != wr_row) {
            int r1, r2;
            if (prev_lo_row < 0) { r1 = lo_row; r2 = lo_row; }
            else if (prev_lo_row < lo_row) { r1 = prev_lo_row; r2 = lo_row; }
            else                           { r1 = lo_row; r2 = prev_lo_row; }
            for (int r = r1; r <= r2; r++) {
                int dy = r - wr_row; if (dy < 0) dy = -dy;
                if (dy > 0) {
                    wattron(win, COLOR_PAIR(SCP_GRAPH_L));
                    mvwaddch(win, r, screen_col, ACS_BULLET);
                    wattroff(win, COLOR_PAIR(SCP_GRAPH_L));
                }
            }
        }
        prev_lo_row = lo_row;

        /* Annotate last column with current rates */
        if (cx == plot_width - 1) {
            wattron(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
            mvwprintw(win, wr_row, screen_col + 1, "%.0f%%", win_rate * 100);
            wattroff(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
            if (lo_row != wr_row) {
                wattron(win, COLOR_PAIR(SCP_GRAPH_L));
                mvwprintw(win, lo_row, screen_col + 1, "%.0f%%", loss_rate * 100);
                wattroff(win, COLOR_PAIR(SCP_GRAPH_L));
            }
        }
    }

    /* Legend */
    int leg_row = plot_bottom + 2;
    int leg_col = plot_left;
    wattron(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
    mvwprintw(win, leg_row, leg_col, "Win rate");
    wattroff(win, COLOR_PAIR(SCP_GRAPH_W) | A_BOLD);
    wattron(win, COLOR_PAIR(SCP_GRAPH_L));
    mvwprintw(win, leg_row, leg_col + 12, "Loss rate");
    wattroff(win, COLOR_PAIR(SCP_GRAPH_L));
    wattron(win, COLOR_PAIR(SCP_HINT) | A_DIM);
    mvwprintw(win, leg_row, leg_col + 25,
              "(rolling %d-game window)", GRAPH_WIN_SIZE);
    wattroff(win, COLOR_PAIR(SCP_HINT) | A_DIM);
}

/*
 * Full stats screen: all numeric sections PLUS the win-rate history
 * graph drawn in the empty space below the numeric data.
 */

void draw_stats_overlay(WINDOW *win, const DchessStats *s)
{
    wclear(win);

    int wh, ww;
    getmaxyx(win, wh, ww);

    wbkgd(win, COLOR_PAIR(CP_CANVAS));
    panel_frame(win, "dchess · statistics", CP_ACC_ENGINE);

    wattron(win, COLOR_PAIR(SCP_HINT));
    const char *hint = " press any key to resume game ";
    mvwprintw(win, wh - 1, (ww - (int)strlen(hint)) / 2, "%s", hint);
    wattroff(win, COLOR_PAIR(SCP_HINT));

    int lm  = 3;
    int cw  = ww - lm * 2;
    int bar = (cw > 50) ? 28 : (cw > 36) ? 20 : 14;
    int row = 2;

    int total_g = s->games_played[0] + s->games_played[1] + s->games_played[2];
    int total_w = s->wins[0]   + s->wins[1]   + s->wins[2];
    int total_l = s->losses[0] + s->losses[1] + s->losses[2];
    int total_d = s->draws[0]  + s->draws[1]  + s->draws[2];

    /* WIN RATE BY DIFFICULTY  */
    draw_section_head(win, row++, lm, cw, "WIN RATE BY DIFFICULTY");
    row++;
    static const char *dname[3] = { "Easy  ", "Medium", "Hard  " };
    for (int d = 0; d < 3; d++) {
        int   g   = s->games_played[d];
        int   w   = s->wins[d];
        int   l   = s->losses[d];
        int   dr  = s->draws[d];
        float pct = g ? 100.0f * w / g : 0.0f;
        int   filled = g ? (int)(bar * w / g) : 0;

        wattron(win, COLOR_PAIR(SCP_LABEL) | A_BOLD);
        mvwprintw(win, row, lm, "%s", dname[d]);
        wattroff(win, COLOR_PAIR(SCP_LABEL) | A_BOLD);
        int bc = lm + 8;
        draw_bar(win, row, bc, filled, bar,
                 COLOR_PAIR(SCP_BAR_WIN) | A_BOLD, COLOR_PAIR(SCP_BAR_BG));
        attr_t va = (pct >= 50) ? COLOR_PAIR(SCP_GOOD) | A_BOLD :
                    (pct >= 30) ? COLOR_PAIR(SCP_NEUT) | A_BOLD :
                                  COLOR_PAIR(SCP_BAD)  | A_BOLD;
        wattron(win, va);
        mvwprintw(win, row, bc + bar + 2, "%5.1f%%", pct);
        wattroff(win, va);
        wattron(win, COLOR_PAIR(SCP_VAL));
        mvwprintw(win, row, bc + bar + 10, "%dW %dL %dD", w, l, dr);
        wattroff(win, COLOR_PAIR(SCP_VAL));
        row++;
    }
    row++;

    /* COLOR PERFORMANCE  */
    if (row + 7 < wh - 2) {
        draw_section_head(win, row++, lm, cw, "COLOR PERFORMANCE");
        row++;
        struct { const char *label; int played; int won; } sides[2] = {
            { "White", s->played_as_white, s->wins_as_white },
            { "Black", s->played_as_black, s->wins_as_black },
        };
        int max_played = 1;
        for (int i = 0; i < 2; i++)
            if (sides[i].played > max_played) max_played = sides[i].played;

        for (int i = 0; i < 2; i++) {
            int   pl  = sides[i].played;
            int   wn  = sides[i].won;
            float pct = pl ? 100.0f * wn / pl : 0.0f;
            int   filled_pl = (int)((float)bar * pl / max_played);
            int   filled_wn = pl ? (int)(bar * wn / pl) : 0;

            int bc = lm + 14;
            wattron(win, COLOR_PAIR(SCP_LABEL) | A_BOLD);
            mvwprintw(win, row, lm, "%-6s played", sides[i].label);
            wattroff(win, COLOR_PAIR(SCP_LABEL) | A_BOLD);
            draw_bar(win, row, bc, filled_pl, bar,
                     COLOR_PAIR(SCP_NEUT), COLOR_PAIR(SCP_BAR_BG));
            wattron(win, COLOR_PAIR(SCP_VAL));
            mvwprintw(win, row, bc + bar + 2, "%d games", pl);
            wattroff(win, COLOR_PAIR(SCP_VAL));
            row++;

            wattron(win, COLOR_PAIR(SCP_LABEL));
            mvwprintw(win, row, lm, "%-6s won   ", sides[i].label);
            wattroff(win, COLOR_PAIR(SCP_LABEL));
            draw_bar(win, row, bc, filled_wn, bar,
                     COLOR_PAIR(SCP_GOOD) | A_BOLD, COLOR_PAIR(SCP_BAR_BG));
            attr_t va = (pct >= 50) ? COLOR_PAIR(SCP_GOOD) | A_BOLD :
                        (pct >= 30) ? COLOR_PAIR(SCP_NEUT) | A_BOLD :
                                      COLOR_PAIR(SCP_BAD)  | A_BOLD;
            wattron(win, va);
            mvwprintw(win, row, bc + bar + 2, "%d wins (%.0f%%)", wn, pct);
            wattroff(win, va);
            row++;
            if (i == 0) row++;
        }
        row++;
    }

    /* PERFORMANCE  */
    if (row + 5 < wh - 2) {
        draw_section_head(win, row++, lm, cw, "PERFORMANCE");
        row++;
        int avg_m  = total_g ? s->total_moves / total_g : 0;
        int avg_t  = total_g ? s->total_time_secs / total_g : 0;
        int long_g = s->longest_game_moves;
        int max_m  = (long_g > 0) ? long_g : 1;
        int bar_m  = (int)((float)bar * avg_m / max_m);
        if (bar_m > bar) bar_m = bar;
        int max_t  = avg_t * 3; if (max_t < 1) max_t = 1;
        int bar_t  = (int)((float)bar * avg_t / max_t);
        if (bar_t > bar) bar_t = bar;
        if (bar_t < 0)   bar_t = 0;

        int bc = lm + 16;
        wattron(win, COLOR_PAIR(SCP_LABEL));
        mvwprintw(win, row, lm, "Avg moves/game");
        wattroff(win, COLOR_PAIR(SCP_LABEL));
        draw_bar(win, row, bc, bar_m, bar,
                 COLOR_PAIR(SCP_NEUT) | A_BOLD, COLOR_PAIR(SCP_BAR_BG));
        wattron(win, COLOR_PAIR(SCP_VAL));
        mvwprintw(win, row, bc + bar + 2, "avg %d  longest %d", avg_m, long_g);
        wattroff(win, COLOR_PAIR(SCP_VAL));
        row++;

        wattron(win, COLOR_PAIR(SCP_LABEL));
        mvwprintw(win, row, lm, "Avg time/game ");
        wattroff(win, COLOR_PAIR(SCP_LABEL));
        draw_bar(win, row, bc, bar_t, bar,
                 COLOR_PAIR(SCP_NEUT) | A_BOLD, COLOR_PAIR(SCP_BAR_BG));
        wattron(win, COLOR_PAIR(SCP_VAL));
        mvwprintw(win, row, bc + bar + 2, "avg %02d:%02d  total %dh %02dm",
                  avg_t / 60, avg_t % 60,
                  s->total_time_secs / 3600,
                  (s->total_time_secs % 3600) / 60);
        wattroff(win, COLOR_PAIR(SCP_VAL));
        row++;
        row++;
    }

    /* OVERALL SCORE  */
    if (row + 4 < wh - 2 && total_g > 0) {
        draw_section_head(win, row++, lm, cw, "OVERALL SCORE");
        row++;
        int bw_score = (cw > 50) ? 40 : (cw > 28) ? cw - 12 : 16;
        int filled_w = (int)((float)bw_score * total_w / total_g);
        int filled_l = (int)((float)bw_score * total_l / total_g);
        int filled_d = bw_score - filled_w - filled_l;
        if (filled_d < 0) filled_d = 0;

        wattron(win, COLOR_PAIR(SCP_LABEL));
        mvwprintw(win, row, lm, "W");
        wattroff(win, COLOR_PAIR(SCP_LABEL));
        for (int i = 0; i < filled_w; i++) {
            wattron(win, COLOR_PAIR(SCP_BAR_WIN) | A_BOLD);
            mvwaddch(win, row, lm + 2 + i, ACS_BLOCK);
            wattroff(win, COLOR_PAIR(SCP_BAR_WIN) | A_BOLD);
        }
        for (int i = 0; i < filled_l; i++) {
            wattron(win, COLOR_PAIR(SCP_BAR_LOSS) | A_BOLD);
            mvwaddch(win, row, lm + 2 + filled_w + i, ACS_BLOCK);
            wattroff(win, COLOR_PAIR(SCP_BAR_LOSS) | A_BOLD);
        }
        for (int i = 0; i < filled_d; i++) {
            wattron(win, COLOR_PAIR(SCP_BAR_DRAW) | A_BOLD);
            mvwaddch(win, row, lm + 2 + filled_w + filled_l + i, ACS_BLOCK);
            wattroff(win, COLOR_PAIR(SCP_BAR_DRAW) | A_BOLD);
        }
        int lx = lm + 2 + bw_score + 2;
        wattron(win, COLOR_PAIR(SCP_GOOD) | A_BOLD);
        mvwprintw(win, row, lx,      "%dW", total_w);
        wattroff(win, COLOR_PAIR(SCP_GOOD) | A_BOLD);
        wattron(win, COLOR_PAIR(SCP_BAD) | A_BOLD);
        mvwprintw(win, row, lx + 5,  "%dL", total_l);
        wattroff(win, COLOR_PAIR(SCP_BAD) | A_BOLD);
        wattron(win, COLOR_PAIR(SCP_NEUT) | A_BOLD);
        mvwprintw(win, row, lx + 10, "%dD", total_d);
        wattroff(win, COLOR_PAIR(SCP_NEUT) | A_BOLD);
        row += 2;

        float ovr = total_g ? 100.0f * total_w / total_g : 0.0f;
        attr_t oa = (ovr >= 50) ? COLOR_PAIR(SCP_GOOD) | A_BOLD :
                    (ovr >= 30) ? COLOR_PAIR(SCP_NEUT) | A_BOLD :
                                  COLOR_PAIR(SCP_BAD)  | A_BOLD;
        wattron(win, oa);
        mvwprintw(win, row, lm, "Overall win rate: %.1f%%  (%d games played)",
                  ovr, total_g);
        wattroff(win, oa);
        row++;
    }

    if (total_g == 0 && row + 2 < wh - 2) {
        wattron(win, COLOR_PAIR(SCP_HINT) | A_BOLD);
        mvwprintw(win, row + 1, lm,
                  "No games recorded yet. Play a game to see stats here!");
        wattroff(win, COLOR_PAIR(SCP_HINT) | A_BOLD);
        row += 2;
    }

    /* WIN RATE HISTORY GRAPH — fills remaining vertical space  */
    row++;
    int graph_height = (wh - 2) - row;
    if (graph_height >= 8) {
        draw_history_graph(win, row, lm, graph_height, cw, s);
    }

    wnoutrefresh(win);
}

/* Blocking full-screen wrapper  */

void show_stats_overlay(const DchessStats *s)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();
    init_colors(0);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    WINDOW *win = newwin(rows, cols, 0, 0);
    keypad(win, TRUE);

    draw_stats_overlay(win, s);
    doupdate();

    wgetch(win);

    delwin(win);
    endwin();
}

/*
 * Small centered popup for the in-game Tab overlay.
 * Creates its own WINDOW, draws stats, waits for a key, then cleans up.
 */
void draw_stats_mini(WINDOW *parent, const DchessStats *s)
{

    int ph, pw;
    getmaxyx(parent, ph, pw);

    /* Popup dimensions */
    int pop_w = 46;
    int pop_h = 18;
    if (pop_w > pw - 4) pop_w = pw - 4;
    if (pop_h > ph - 4) pop_h = ph - 4;
    /* newwin() takes SCREEN coordinates, but the centring above is
     * relative to `parent`. Without adding the parent's own origin the
     * popup lands too far left and sits on top of the info panel
     * instead of over the board. */
    int par_r, par_c;
    getbegyx(parent, par_r, par_c);
    int pop_r = par_r + (ph - pop_h) / 2;
    int pop_c = par_c + (pw - pop_w) / 2;

    WINDOW *shadow = panel_shadow(pop_h, pop_w, pop_r, pop_c);
    WINDOW *pop    = newwin(pop_h, pop_w, pop_r, pop_c);
    wbkgd(pop, COLOR_PAIR(CP_CANVAS));
    keypad(pop, TRUE);

    panel_frame(pop, "statistics", CP_ACC_ENGINE);

    /* Dismiss hint */
    wattron(pop, COLOR_PAIR(SCP_HINT));
    const char *hint = " any key to close ";
    mvwprintw(pop, pop_h - 1, (pop_w - (int)strlen(hint)) / 2, "%s", hint);
    wattroff(pop, COLOR_PAIR(SCP_HINT));

    int total_g = s->games_played[0] + s->games_played[1] + s->games_played[2];
    int total_w = s->wins[0]   + s->wins[1]   + s->wins[2];
    int total_l = s->losses[0] + s->losses[1] + s->losses[2];
    int total_d = s->draws[0]  + s->draws[1]  + s->draws[2];
    float ovr   = total_g ? 100.0f * total_w / total_g : 0.0f;

    int row = 2;
    int lm  = 2;

    /* Overall W/L/D ── */
    wattron(pop, COLOR_PAIR(SCP_HEAD) | A_BOLD);
    mvwprintw(pop, row++, lm, "Overall  (%d games)", total_g);
    wattroff(pop, COLOR_PAIR(SCP_HEAD) | A_BOLD);

    if (total_g == 0) {
        wattron(pop, COLOR_PAIR(SCP_HINT));
        mvwprintw(pop, row++, lm, "No games yet — play one first!");
        wattroff(pop, COLOR_PAIR(SCP_HINT));
    } else {
        /* W/L/D mini bar */
        int bar_w = pop_w - lm * 2 - 2;
        int fw = (int)((float)bar_w * total_w / total_g);
        int fl = (int)((float)bar_w * total_l / total_g);
        int fd = bar_w - fw - fl;
        if (fd < 0) fd = 0;
        int cx = lm + 1;
        for (int i = 0; i < fw; i++) {
            wattron(pop, COLOR_PAIR(SCP_BAR_WIN) | A_BOLD);
            mvwaddch(pop, row, cx++, ACS_BLOCK);
            wattroff(pop, COLOR_PAIR(SCP_BAR_WIN) | A_BOLD);
        }
        for (int i = 0; i < fl; i++) {
            wattron(pop, COLOR_PAIR(SCP_BAR_LOSS) | A_BOLD);
            mvwaddch(pop, row, cx++, ACS_BLOCK);
            wattroff(pop, COLOR_PAIR(SCP_BAR_LOSS) | A_BOLD);
        }
        for (int i = 0; i < fd; i++) {
            wattron(pop, COLOR_PAIR(SCP_BAR_DRAW));
            mvwaddch(pop, row, cx++, ACS_BLOCK);
            wattroff(pop, COLOR_PAIR(SCP_BAR_DRAW));
        }
        row++;

        /* Numeric summary */
        attr_t oa = (ovr >= 50) ? COLOR_PAIR(SCP_GOOD) | A_BOLD :
                    (ovr >= 30) ? COLOR_PAIR(SCP_NEUT) | A_BOLD :
                                  COLOR_PAIR(SCP_BAD)  | A_BOLD;
        wattron(pop, oa);
        mvwprintw(pop, row, lm, "Win rate: %.1f%%", ovr);
        wattroff(pop, oa);
        wattron(pop, COLOR_PAIR(SCP_VAL));
        mvwprintw(pop, row, lm + 17, "%dW  %dL  %dD", total_w, total_l, total_d);
        wattroff(pop, COLOR_PAIR(SCP_VAL));
        row += 2;

        /* Per-difficulty ── */
        wattron(pop, COLOR_PAIR(SCP_HEAD) | A_BOLD);
        mvwprintw(pop, row++, lm, "By difficulty");
        wattroff(pop, COLOR_PAIR(SCP_HEAD) | A_BOLD);

        static const char *dname[3] = { "Easy  ", "Medium", "Hard  " };
        int bar_d = pop_w - lm * 2 - 18;   /* leave room: 7 label + bar + 2 gap + 5 pct + 2 gap + 5 count */
        if (bar_d < 6) bar_d = 6;

        for (int d = 0; d < 3; d++) {
            int g   = s->games_played[d];
            int w   = s->wins[d];
            float p = g ? 100.0f * w / g : 0.0f;
            int  fb = g ? (int)(bar_d * w / g) : 0;

            wattron(pop, COLOR_PAIR(SCP_LABEL) | A_BOLD);
            mvwprintw(pop, row, lm, "%s", dname[d]);
            wattroff(pop, COLOR_PAIR(SCP_LABEL) | A_BOLD);

            int bx = lm + 7;
            for (int i = 0; i < bar_d; i++) {
                if (i < fb) {
                    wattron(pop, COLOR_PAIR(SCP_BAR_WIN) | A_BOLD);
                    mvwaddch(pop, row, bx + i, ACS_BLOCK);
                    wattroff(pop, COLOR_PAIR(SCP_BAR_WIN) | A_BOLD);
                } else {
                    wattron(pop, COLOR_PAIR(SCP_BAR_BG));
                    mvwaddch(pop, row, bx + i, ACS_BULLET);
                    wattroff(pop, COLOR_PAIR(SCP_BAR_BG));
                }
            }
            attr_t pa = (p >= 50) ? COLOR_PAIR(SCP_GOOD) | A_BOLD :
                        (p >= 30) ? COLOR_PAIR(SCP_NEUT) | A_BOLD :
                                    COLOR_PAIR(SCP_BAD)  | A_BOLD;
            wattron(pop, pa);
            mvwprintw(pop, row, bx + bar_d + 1, "%4.0f%%", p);
            wattroff(pop, pa);
            wattron(pop, COLOR_PAIR(SCP_HINT));
            mvwprintw(pop, row, bx + bar_d + 7, "%2dg", g);
            wattroff(pop, COLOR_PAIR(SCP_HINT));
            row++;
        }
        row++;

        /* Longest game & avg time ── */
        if (row < pop_h - 2) {
            int avg_t = total_g ? s->total_time_secs / total_g : 0;
            wattron(pop, COLOR_PAIR(SCP_VAL));
            mvwprintw(pop, row, lm, "Avg time  %02d:%02d   Longest  %d moves",
                      avg_t / 60, avg_t % 60, s->longest_game_moves);
            wattroff(pop, COLOR_PAIR(SCP_VAL));
        }
    }

    touchwin(pop);
    wrefresh(pop);
    wgetch(pop);
    delwin(pop);
    panel_shadow_destroy(shadow);
}
