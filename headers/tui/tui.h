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

/* ── The TUI's own state ─────────────────────────────────────────────────
 *
 * What is on screen and how the player is interacting with it. The game
 * being played lives in `game` (see game/game.h) and the rules logic
 * belongs there, not here -- this struct is the view and the controller.
 *
 * The split matters: this file used to hold the position, the move log,
 * the clocks, the draw-rule bookkeeping, the cursor AND the search
 * thread in one flat struct, which is how the same move-committing code
 * ended up written twice and a stale search result ended up playable
 * onto a board it was never computed for. */
typedef struct {
    /* The game being played. Advance it only via game_play(). */
    GameState game;

    char     status[256];
    char     last_cmd[64];
    int      engine_depth;
    int      engine_side;
    char     last_eval[32];   /* formatted for display, e.g. "+0.34" */

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

    /* Vim-style input mode: 0 = normal (hjkl navigate), 1 = insert (type commands) */
    int  insert_mode;

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

    /* ── Background engine search ─────────────────────────────────────────
     * The engine's search() runs on a separate thread so the UI stays
     * responsive (clock ticking, redraws, quit) while it thinks, instead
     * of the whole process blocking for up to time_limit_ms.
     *
     * search_snapshot is a PRIVATE COPY of the position taken at the
     * moment the search is kicked off -- the worker thread only ever
     * touches this copy, never the live `pos` above, so the main thread
     * can keep safely reading/rendering `pos` the entire time a search
     * is in flight. `pos` itself is only updated afterward, on the main
     * thread, once the result has been collected.
     *
     * search_running is only ever touched by the main thread (set before
     * pthread_create, cleared after pthread_join) -- it's how the rest of
     * the UI knows a search is in flight and should reject any command
     * that would otherwise race with it (a new move, a new game, loading
     * a different position, another concurrent search() call, etc.).
     * search_ready/search_result are the one genuine cross-thread
     * handoff and are protected by search_mutex: the worker thread locks,
     * writes the result, sets search_ready, and unlocks just before it
     * exits; the main thread's poll_engine_search() (see commands.c)
     * locks, checks/copies/clears, and unlocks once per main-loop
     * iteration (~every 100ms, via the existing redraw timeout). */
    pthread_t       search_thread;
    pthread_mutex_t search_mutex;
    int             search_running;
    int             search_ready;
    Position        search_snapshot;
    int             search_depth_arg;     /* engine_depth, captured at kickoff */
    int             search_time_limit_arg; /* time_limit_ms, captured at kickoff */
    SearchResult    search_result;

    /* hash_position(search_snapshot), captured at kickoff. A completed
     * result is only applied if the live `pos` still hashes to this --
     * i.e. the board the engine was thinking about is still the board on
     * screen. Without it, anything that mutates `pos` mid-search (a
     * cursor move, a new game started from the game-over popup) would
     * have the stale result played on top of it, moving a piece that is
     * no longer there. Cheaper and more robust than trying to enumerate
     * every path that can touch `pos`. */
    U64             search_snapshot_hash;
} TUIState;

/* Pass CLI config so tui_init can configure engine side & depth */
void tui_init(TUIState *state, const CliArgs *args);
void tui_run(TUIState *state);
void tui_cleanup(void);

#endif
