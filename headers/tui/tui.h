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
#include "game/replay.h"
#include "game/analysis.h"

/* View and controller. The game itself lives in `game`, and the rules
 * logic belongs there, not here. */
/* A replay's game review: one analysis per position, filled in turn. */
typedef struct {
    Analysis      at[MAX_MOVE_HISTORY + 1];
    unsigned char have[MAX_MOVE_HISTORY + 1];
    int           next, running, waiting, first_side;
    U64           key;
    Analyser     *an;
} ReplayReview;

struct PuzzleView;

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
    ProfileList profiles;
    Book     *book;          /* opening book for the built-in engine, or NULL */
    char      book_choice[256];   /* builtin, off or a .bin path */
    char      cli_book[256];      /* --book, kept for this run only, never saved */
    char      cli_clock[32];      /* --clock, likewise */
    int       last_was_book;      /* last_search came from the book */
    int       file_active;    /* active profile as saved; --profile does not change it */
    int       theme_set;      /* theme chosen by flag or launcher, so it is remembered */
    int       cli_setup;      /* players came from flags; the launcher keeps them */
    char      engine_error[400];   /* the last engine failure, until the next search */

    /* 0 = normal (hjkl navigates), 1 = insert (type commands) */
    int  insert_mode;

    /* Side the board is drawn from; turns each move between two humans. */
    int  view_side;

    /* Lets commands.c force a repaint so "Engine thinking..." appears
     * immediately. NULL until tui_run() installs it. */
    void (*request_redraw)(void *ctx);
    void  *redraw_ctx;


    int show_onboarding;
    /* Replay mode: the game is shown at replay_ply of *replay. */
    ReplayGame *replay;
    ReplayReview *review;
    int         replay_ply, replay_auto;
    ReplayList *replay_list;             /* --replay: the file's games */
    /* Analysis: an engine's view of the position shown. */
    Analyser   *analyser;
    int         analysis_on, analysis_ready, analysis_blocked;
    Analysis    analysis;
    U64         analysis_key;
    char        analysis_engine[ENGINE_NAME_MAX + 1];   /* "", off, builtin or a name */
    char        analysis_err[128];

    int         stats_only;              /* --stats: the stats page, not the launcher */
    int         puzzles_only;            /* --puzzles: the puzzles menu, not the launcher */
    const struct PuzzleView *puzzle;     /* set while a puzzle is on the board */
    int         human_active[2];         /* this side is the active profile */
    char        replay_path[512];

    int first_run;           /* no profiles.conf yet: the welcome creates it; 2 = unsaved */

    /* Index into the theme table; applied via init_colors(). */
    int theme;
} TUIState;


void tui_init(TUIState *state, const CliArgs *args);
void tui_run(TUIState *state);
void tui_cleanup(void);

/* A profile's remembered white/black word as a player. */
Player tui_word_player(const TUIState *s, const char *w, Player fallback);

/* Arrows / hjkl move the cursor; Enter picks up a piece, then its square.
 * 1 when that chose a legal move (in *out, not played), 0 when the key was
 * handled, -1 when it is not a cursor key. */
int tui_cursor_key(TUIState *s, int ch, Move *out);

/* The game screen, for modes that draw it outside tui_run(). */
typedef struct Screen Screen;
Screen *tui_screen_open(TUIState *state);
void    tui_screen_paint(Screen *sc);
void    tui_screen_resize(Screen *sc);
WINDOW *tui_screen_input(Screen *sc);
void    tui_screen_close(Screen *sc);

#endif
