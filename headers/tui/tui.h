#ifndef TUI_H
#define TUI_H

#include "engine/board.h"
#include "engine/search.h"
#include "game/game.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <ncurses.h>
#include <time.h>
#include <pthread.h>

/* View and controller. The game itself lives in `game`, and the rules
 * logic belongs there, not here. */
typedef struct {
    /* Advance only via game_play(). */
    GameState game;

    char     status[256];
    char     last_cmd[64];
    int      engine_depth;
    int      engine_side;
    char     last_eval[32];   /* formatted for display, e.g. "+0.34" */
    SearchResult last_search; /* nodes == 0 until the first search */

    /* Cursor & selection */
    int      cursor_row;
    int      cursor_col;
    int      selected;
    int      sel_row;
    int      sel_col;
    int      highlight[8][8];

    /* Game configuration from CLI */
    int      player_side;   /* WHITE or BLACK  – the human's color */
    int      difficulty;    /* DIFF_EASY / DIFF_MEDIUM / DIFF_HARD */

    /* Persistent statistics */
    DchessStats stats;

    /* 0 = normal (hjkl navigates), 1 = insert (type commands) */
    int  insert_mode;

    /* Local two-player: no engine. */
    int  two_player;

    /* Side the board is drawn from; flips each move in two-player. */
    int  view_side;

    /* Lets commands.c force a repaint so "Engine thinking..." appears
     * immediately. NULL until tui_run() installs it. */
    void (*request_redraw)(void *ctx);
    void  *redraw_ctx;


    int show_onboarding;

    /* Index into the theme table; applied via init_colors(). */
    int theme;

    /* Search budget in ms, paired with engine_depth. */
    int time_limit_ms;

    /* Background engine search. The worker only ever touches
     * search_snapshot, a private copy, so the main thread can keep
     * rendering the live game while a search is in flight.
     *
     * search_running is main-thread only. search_ready/search_result are
     * the one cross-thread handoff and are guarded by search_mutex. */
    pthread_t       search_thread;
    pthread_mutex_t search_mutex;
    int             search_running;
    int             search_ready;
    Position        search_snapshot;
    int             search_depth_arg;     /* engine_depth, captured at kickoff */
    int             search_time_limit_arg; /* time_limit_ms, captured at kickoff */
    SearchResult    search_result;

    /* Captured at kickoff. A result is only applied if the live position
     * still hashes to this, so anything that changed the board
     * mid-search invalidates it. */
    U64             search_snapshot_hash;
} TUIState;


void tui_init(TUIState *state, const CliArgs *args);
void tui_run(TUIState *state);
void tui_cleanup(void);

#endif
