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
    int             white_clock;      /* accumulated seconds */
    int             black_clock;
    time_t          turn_start;       /* when the current turn began (seconds) */
    struct timespec turn_start_mono;  /* high-res start for centisecond display */

    /* Draw detection */
    int      halfmove_clock;          /* moves since last pawn move or capture */
    U64      pos_history[MAX_MOVE_HISTORY]; /* hash of each position for repetition */
    int      pos_history_count;

    /* Game configuration from CLI */
    int      player_side;   /* WHITE or BLACK  – the human's color */
    int      difficulty;    /* DIFF_EASY / DIFF_MEDIUM / DIFF_HARD */

    /* Persistent statistics */
    DchessStats stats;

    /* Evaluation history (centipawns, one entry per half-move) */
    int  eval_history[MAX_MOVE_HISTORY];
    int  eval_count;

    /* Vim-style input mode: 0 = normal (hjkl navigate), 1 = insert (type commands) */
    int  insert_mode;

    /* Side that owns the currently-ticking clock (WHITE or BLACK),
     * updated whenever a move is committed so rendering is correct even
     * while the engine is thinking synchronously. */
    int  clock_side;

    /* Set to 1 once the first move of the game has been made;
     * clocks don't tick until then. */
    int  clock_started;

    /* Two-player (local) mode: no engine, board flips after each move */
    int  two_player;

    /* The side whose perspective the board is currently rendered from.
     * In single-player this equals player_side and never changes.
     * In two-player this flips between WHITE and BLACK after each move. */
    int  view_side;

    /* Lets code outside tui.c (engine_move() in commands.c) force an
     * immediate screen repaint before a long blocking call, so status
     * text like "Engine thinking..." is actually painted to the
     * terminal instead of looking frozen until search() returns.
     * Installed by tui_run(); NULL (and safely skipped) if unset. */
    void (*request_redraw)(void *ctx);
    void  *redraw_ctx;

    /* Set by tui_init() from CliArgs; tui_run() checks this once, right
     * after ncurses is up, to decide whether to show the interactive
     * onboarding screen before creating the game windows. */
    int show_onboarding;

    /* Color theme index (see render.h theme_name()/theme_from_name()).
     * Applied via init_colors(); can change live from the onboarding
     * screen or the in-game "theme <name>" command. */
    int theme;

    /* Wall-clock time budget (ms) for search()'s iterative deepening,
     * paired with engine_depth via cli_time_limit_for_difficulty(). */
    int time_limit_ms;
} TUIState;

/* Pass CLI config so tui_init can configure engine side & depth */
void tui_init(TUIState *state, const CliArgs *args);
void tui_run(TUIState *state);
void tui_cleanup(void);

#endif
