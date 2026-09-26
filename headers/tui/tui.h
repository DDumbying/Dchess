#ifndef TUI_H
#define TUI_H

#include "engine/board.h"
#include "engine/search.h"
#include "game/game.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <ncurses.h>
#include <time.h>
#include "game/players.h"
#include "game/opponent.h"

/* View and controller. The game itself lives in `game`, and the rules
 * logic belongs there, not here. */
typedef struct {
    /* Advance only via game_play(). */
    GameState game;

    char     status[256];
    char     last_cmd[64];
    char     last_eval[32];   /* formatted for display, e.g. "+0.34" */
    SearchResult last_search; /* nodes == 0 until the first search */
    char     last_search_by[48];   /* the engine behind last_search */

    /* Cursor & selection */
    int      cursor_row;
    int      cursor_col;
    int      selected;
    int      sel_row;
    int      sel_col;
    int      highlight[8][8];

    /* Who plays each side. Drivers are built by tui_attach_players(), not
     * tui_init(), which runs twice and would leak the first set. */
    Player    players[2];
    Opponent *drivers[2];     /* NULL for a human */
    Opponent *go_driver;      /* "go" on a human's turn */
    Opponent *thinking;       /* the driver searching now, or NULL */
    char      thinking_by[48];
    int       paused;
    long      last_move_ms;   /* monotonic; when an engine last moved */
    EngineList engines;       /* engines.conf, as of the last load */
    char      engine_error[400];   /* the last engine failure, until the next search */

    /* Persistent statistics */
    DchessStats stats;

    /* 0 = normal (hjkl navigates), 1 = insert (type commands) */
    int  insert_mode;

    /* Side the board is drawn from; turns each move between two humans. */
    int  view_side;

    /* Lets commands.c force a repaint so "Engine thinking..." appears
     * immediately. NULL until tui_run() installs it. */
    void (*request_redraw)(void *ctx);
    void  *redraw_ctx;


    int show_onboarding;

    /* Index into the theme table; applied via init_colors(). */
    int theme;
} TUIState;


void tui_init(TUIState *state, const CliArgs *args);
void tui_run(TUIState *state);
void tui_cleanup(void);

#endif
