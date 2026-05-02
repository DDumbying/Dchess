#ifndef TUI_H
#define TUI_H

#include "engine/board.h"
#include "engine/search.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <ncurses.h>
#include <time.h>

#define MAX_MOVE_HISTORY 256

typedef struct {
    Position pos;
    char     move_history[MAX_MOVE_HISTORY][8];
    int      move_piece[MAX_MOVE_HISTORY];
    int      move_time[MAX_MOVE_HISTORY];
    int      move_count;
    char     status[256];
    char     last_cmd[64];
    int      engine_depth;
    int      engine_side;
    int      game_over;
    char     game_result[64];
    char     last_eval[32];

    /* Cursor & selection */
    int      cursor_row;
    int      cursor_col;
    int      selected;
    int      sel_row;
    int      sel_col;
    int      highlight[8][8];

    /* Clocks: total seconds spent by each side */
    int      white_clock;   /* accumulated seconds */
    int      black_clock;
    time_t   turn_start;    /* when the current turn began */

    /* Draw detection */
    int      halfmove_clock;          /* moves since last pawn move or capture */
    U64      pos_history[MAX_MOVE_HISTORY]; /* hash of each position for repetition */
    int      pos_history_count;

    /* Game configuration from CLI */
    int      player_side;   /* WHITE or BLACK  – the human's color */
    int      difficulty;    /* DIFF_EASY / DIFF_MEDIUM / DIFF_HARD */

    /* Persistent statistics */
    DchessStats stats;
} TUIState;

/* Pass CLI config so tui_init can configure engine side & depth */
void tui_init(TUIState *state, const CliArgs *args);
void tui_run(TUIState *state);
void tui_cleanup(void);

#endif
