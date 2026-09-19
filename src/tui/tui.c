#include "tui/tui.h"
#include "tui/render.h"
#include "tui/colors.h"
#include "tui/panel.h"
#include "tui/input.h"
#include "tui/commands.h"
#include "tui/stats_tui.h"
#include "tui/onboard.h"
#include "engine/board.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "engine/hash.h"
#include "engine/fen.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


/* The panels below are defined before Screen but must rebuild it on
 * resize. */
typedef struct Screen Screen;
static void screen_handle_resize(Screen *sc);
static WINDOW *screen_board(const Screen *sc);

/* Game-over popup  */
/* Split out from the input loop so a resize can rebuild both windows. */
static void build_game_over_panel(WINDOW *board_win, const TUIState *state,
                                  WINDOW **out_pop, WINDOW **out_shadow)
{
    int bh, bw;
    getmaxyx(board_win, bh, bw);

    int pw = 46, ph = 9;
    if (pw > bw) pw = bw;
    if (ph > bh) ph = bh;

    /* newwin() takes SCREEN coordinates but the centring is relative to
     * board_win, so its origin has to be added. */
    int bwr, bwc;
    getbegyx(board_win, bwr, bwc);
    int pr     = bwr + (bh - ph) / 2;
    int pc_col = bwc + (bw - pw) / 2;

    WINDOW *shadow = panel_shadow(ph, pw, pr, pc_col);
    WINDOW *pop    = newwin(ph, pw, pr, pc_col);
    keypad(pop, TRUE);

    wattron(pop, COLOR_PAIR(CP_BORDER));
    box(pop, ACS_VLINE, ACS_HLINE);
    wattroff(pop, COLOR_PAIR(CP_BORDER));

    wattron(pop, COLOR_PAIR(CP_TITLE)|A_BOLD);
    mvwprintw(pop, 0, (pw-11)/2, " GAME OVER ");
    wattroff(pop, COLOR_PAIR(CP_TITLE)|A_BOLD);

    wattron(pop, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);
    int rlen = (int)strlen(state->game.result);
    mvwprintw(pop, 2, (pw - rlen) / 2, "%s", state->game.result);
    wattroff(pop, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);

    int total_moves = (state->game.move_count + 1) / 2;
    int ws = state->game.white_clock, bs = state->game.black_clock;
    wattron(pop, COLOR_PAIR(CP_HINT));
    mvwprintw(pop, 4, 4, "Moves  : %d", total_moves);
    mvwprintw(pop, 5, 4, "White  : %02d:%02d   Black : %02d:%02d",
              ws/60, ws%60, bs/60, bs%60);
    mvwprintw(pop, 6, 4, "Eval   : %s", state->last_eval);
    wattroff(pop, COLOR_PAIR(CP_HINT));

    wattron(pop, COLOR_PAIR(CP_STATUS_OK)|A_BOLD);
    mvwprintw(pop, 7, 6, "[ R ] New game        [ Q ] Quit");
    wattroff(pop, COLOR_PAIR(CP_STATUS_OK)|A_BOLD);

    wrefresh(pop);

    *out_pop    = pop;
    *out_shadow = shadow;
}

static void show_game_over_popup(Screen *sc, TUIState *state)
{
    WINDOW *pop, *shadow;
    build_game_over_panel(screen_board(sc), state, &pop, &shadow);

    /* Save stats for this completed game  */
    {
        int result = 0; /* draw by default */
        const char *r = state->game.result;
        /* Check if human won or lost */
        if (strstr(r, "White wins")) {
            result = (state->player_side == WHITE) ? 1 : -1;
        } else if (strstr(r, "Black wins")) {
            result = (state->player_side == BLACK) ? 1 : -1;
        }
        int total_secs = state->game.white_clock + state->game.black_clock;
        stats_record(&state->stats,
                     state->difficulty,
                     result,
                     state->player_side,
                     state->game.move_count,
                     total_secs);
        stats_save(&state->stats);
    }

    while (1) {
        int ch = wgetch(pop);

        if (ch == KEY_RESIZE) {
            /* Rebuilds the board behind it too, since it swallowed the
             * resize the main loop would have acted on. Stats are
             * recorded outside this loop, so this cannot re-record. */
            delwin(pop);
            panel_shadow_destroy(shadow);
            screen_handle_resize(sc);
            build_game_over_panel(screen_board(sc), state, &pop, &shadow);
            continue;
        }

        if (ch == 'r' || ch == 'R') {
            /* Also cancels any search: the engine can still be thinking
             * behind this popup when a move ended the game by a draw
             * rule. */
            tui_new_game(state);
            break;
        }
        if (ch == 'q' || ch == 'Q') {
            /* The worker writes into TUIState, which lives in main()'s
             * frame, so stop it before exit() tears down. */
            cancel_engine_search(state);
            delwin(pop);
            panel_shadow_destroy(shadow);
            tui_cleanup();
            exit(0);
        }
    }
    delwin(pop);
    panel_shadow_destroy(shadow);
}

/* The board is drawn from view_side's perspective: row 0 is rank 8 as
 * White, rank 1 as Black. */

static int screen_to_square(const TUIState *state, int row, int col)
{
    int flipped = (state->view_side == BLACK);
    int rank = flipped ? row : (7 - row);
    int file = flipped ? (7 - col) : col;
    return rank * 8 + file;
}

static void square_to_screen(const TUIState *state, int sq, int *row, int *col)
{
    int flipped = (state->view_side == BLACK);
    int rank = sq / 8, file = sq % 8;
    *row = flipped ? rank : (7 - rank);
    *col = flipped ? (7 - file) : file;
}

static void build_highlights(TUIState *state)
{
    memset(state->highlight, 0, sizeof(state->highlight));
    if (!state->selected) return;

    int from = screen_to_square(state, state->sel_row, state->sel_col);

    MoveList ml;
    generate_moves(&state->game.pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from) continue;

        Position tmp = state->game.pos;
        if (!make_move(&tmp, m)) continue;   /* pseudo-legal: filter here */

        int row, col;
        square_to_screen(state, TO(m), &row, &col);
        state->highlight[row][col] = 1;
    }
}

static void clear_selection(TUIState *state)
{
    state->selected = 0;
    memset(state->highlight, 0, sizeof(state->highlight));
}

/* First Enter: pick up a piece. */
static void select_square(TUIState *state, int sq)
{
    int piece = game_piece_at(&state->game, sq);
    int friendly = (piece >= 0) &&
                   ((state->game.pos.side == WHITE && piece < 6) ||
                    (state->game.pos.side == BLACK && piece >= 6));

    if (!friendly) {
        snprintf(state->status, sizeof(state->status),
                 "No friendly piece on %c%d", 'a' + (sq % 8), (sq / 8) + 1);
        return;
    }

    state->selected = 1;
    state->sel_row  = state->cursor_row;
    state->sel_col  = state->cursor_col;
    build_highlights(state);
    snprintf(state->status, sizeof(state->status),
             "Selected %c%d — move cursor to destination and press Enter",
             'a' + (sq % 8), (sq / 8) + 1);
}

/* Second Enter: play it to the cursor square. */
static void move_to_square(TUIState *state, int to_sq)
{
    int from_sq = screen_to_square(state, state->sel_row, state->sel_col);

    Move m;
    if (!game_find_move(&state->game, from_sq, to_sq, 0, &m)) {
        if (!state->highlight[state->cursor_row][state->cursor_col])
            snprintf(state->status, sizeof(state->status),
                     "Not a legal move — select a highlighted square.");
        clear_selection(state);
        return;
    }

    char text[8];
    move_to_str(m, text);
    game_play(&state->game, m);
    snprintf(state->status, sizeof(state->status), "Played: %s", text);

    game_update_status(&state->game);
    clear_selection(state);
    if (state->game.game_over) return;

    if (state->two_player) {
        state->view_side  = state->game.pos.side;
        state->cursor_row = 6;
        state->cursor_col = 4;
    } else if (state->engine_side == state->game.pos.side) {
        /* Only kicks off the search; the main loop applies the result. */
        handle_command(state, "go");
    }
}

static void cursor_enter(TUIState *state)
{
    int sq = screen_to_square(state, state->cursor_row, state->cursor_col);

    if (!state->selected) {
        select_square(state, sq);
        return;
    }

    /* Enter on the already-selected square means "put it back down". */
    if (state->cursor_row == state->sel_row &&
        state->cursor_col == state->sel_col) {
        clear_selection(state);
        snprintf(state->status, sizeof(state->status), "Deselected.");
        return;
    }

    move_to_square(state, sq);
}

void tui_init(TUIState *state, const CliArgs *args)
{
    memset(state, 0, sizeof(*state));

    /* tui_init() can run twice (CLI defaults, then again from onboarding
     * with the player's choices) -- always before any background search
     * could possibly be in flight, so re-initializing an all-zeroed,
     * never-locked mutex here is safe in practice even without an
     * explicit destroy first. */
    pthread_mutex_init(&state->search_mutex, NULL);

    /* Apply CLI configuration */
    state->player_side  = args ? args->player_side  : WHITE;
    state->difficulty   = args ? args->difficulty   : DIFF_MEDIUM;
    state->engine_depth = args ? args->engine_depth : 5;
    state->two_player   = args ? args->two_player   : 0;
    state->time_limit_ms = cli_time_limit_for_difficulty(state->difficulty);
    state->show_onboarding = args ?
        (args->menu || (!args->any_gameplay_flag && !args->no_menu)) : 0;
    state->theme = args ? args->theme : 0;

    /* Starting position: a custom FEN if one was given (and it is still
     * valid -- cli_parse() already validated it, but a FEN chosen via the
     * onboarding screen comes through this same path, so re-check rather
     * than assume), otherwise the standard setup. */
    game_reset(&state->game);
    int fen_ok = 0;
    if (args && args->fen[0])
        fen_ok = game_load_fen(&state->game, args->fen);

    /* Engine plays the opposite side of the human (disabled in two-player) */
    state->engine_side = state->two_player ? -1 :
                         (state->player_side == WHITE) ? BLACK : WHITE;

    /* The board always opens from White's perspective */
    state->view_side  = WHITE;
    state->cursor_row = 6;
    state->cursor_col = 4;

    stats_load(&state->stats);
    snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");

    char setup[128];
    describe_setup(state, setup, sizeof(setup));
    if (args && args->fen[0] && !fen_ok) {
        snprintf(state->status, sizeof(state->status),
                 "Invalid --fen, started standard game instead | %s", setup);
    } else {
        snprintf(state->status, sizeof(state->status),
                 "Ready – %s | arrows/hjkl=move  enter=select", setup);
    }
}

void tui_cleanup(void) { endwin(); }

/* The four windows the game is drawn into, plus the paint routine. */
struct Screen {
    WINDOW   *board, *info, *eval_bar, *cmd;
    TUIState *state;
};

/* Smallest terminal that fits a COMPLETE board: eight ranks plus file
 * labels, the clock row, five status rows and two borders. Being too
 * permissive here silently clips rank 1 off the bottom. */
#define MIN_ROWS 20
#define MIN_COLS 34

/* Replaces windows in place, so TUIState.redraw_ctx -- which points at
 * the caller's Screen -- stays valid across a resize. */
static void screen_build(Screen *sc)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    const int cmd_h = 3;
    int main_h = rows - cmd_h;

    int info_w     = (cols >= 90) ? 32 : (cols >= 70) ? 26 : (cols >= 55) ? 20 : 0;
    int eval_bar_w = (cols >= 55) ? 3 : 0;
    int board_w    = cols - info_w - eval_bar_w;

    sc->board    = newwin(main_h, board_w, 0, info_w + eval_bar_w);
    sc->info     = info_w     ? newwin(main_h, info_w,     0, 0)      : NULL;
    sc->eval_bar = eval_bar_w ? newwin(main_h, eval_bar_w, 0, info_w) : NULL;
    sc->cmd      = newwin(cmd_h, cols, main_h, 0);

    wbkgd(stdscr, COLOR_PAIR(CP_CANVAS));
    werase(stdscr);
    wrefresh(stdscr);

    keypad(sc->board, TRUE);
    keypad(sc->cmd,   TRUE);

    /* Wake up every 100ms even without input, so the clocks tick live. */
    wtimeout(sc->cmd, 100);
}

/* [INFO][EVAL BAR][BOARD] above a command line; the first two are
 * dropped on narrow terminals. */
static Screen screen_create(TUIState *state)
{
    Screen sc;
    memset(&sc, 0, sizeof(sc));
    sc.state = state;
    screen_build(&sc);
    return sc;
}

static void screen_free_windows(Screen *sc)
{
    if (sc->board)    delwin(sc->board);
    if (sc->eval_bar) delwin(sc->eval_bar);
    if (sc->info)     delwin(sc->info);
    if (sc->cmd)      delwin(sc->cmd);
    sc->board = sc->info = sc->eval_bar = sc->cmd = NULL;
}

static void screen_destroy(Screen *sc)
{
    screen_free_windows(sc);
}

static int screen_too_small(void)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    return rows < MIN_ROWS || cols < MIN_COLS;
}

/* Drawn on stdscr: the game's own windows do not exist at this point. */
static void screen_draw_too_small(void)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    werase(stdscr);
    const char *msg = "Terminal too small";
    char detail[64];
    snprintf(detail, sizeof(detail), "need %dx%d, have %dx%d",
             MIN_COLS, MIN_ROWS, cols, rows);

    if (rows > 0 && cols > (int)strlen(msg))
        mvwprintw(stdscr, rows / 2, (cols - (int)strlen(msg)) / 2, "%s", msg);
    if (rows > 2 && cols > (int)strlen(detail))
        mvwprintw(stdscr, rows / 2 + 1, (cols - (int)strlen(detail)) / 2, "%s", detail);
    wrefresh(stdscr);
}

static void screen_paint(const Screen *sc)
{
    WINDOW *wins[] = { stdscr, sc->board, sc->info, sc->eval_bar, sc->cmd };
    const int n = (int)(sizeof(wins) / sizeof(wins[0]));

    werase(stdscr);
    wnoutrefresh(stdscr);
    render_all(sc->board, sc->info, sc->eval_bar, sc->cmd, sc->state);

    for (int i = 0; i < n; i++) if (wins[i]) touchwin(wins[i]);
    for (int i = 0; i < n; i++) if (wins[i]) wnoutrefresh(wins[i]);
    doupdate();
}

/* Adapter for TUIState.request_redraw, which cannot know about Screen. */
static void screen_paint_hook(void *ctx) { screen_paint((const Screen *)ctx); }

static WINDOW *screen_board(const Screen *sc) { return sc->board; }

/* Key handling  */

/* Blocks while the terminal is too small: there is no smaller layout to
 * fall back to. */
static void screen_handle_resize(Screen *sc)
{
    screen_free_windows(sc);

    while (screen_too_small()) {
        screen_draw_too_small();
        /* Blocks until a key or the next resize. The game is paused
         * meanwhile -- clocks stop, searches are not polled. */
        wgetch(stdscr);
    }

    screen_build(sc);
    screen_paint(sc);
}

/* Returns 0 when the player asked to quit, 1 to keep going. */
static int handle_key(Screen *sc, int ch, const char *cmd_buf)
{
    TUIState *state = sc->state;

    switch (ch) {
        case KEY_UP:    case 'k':
            if (state->cursor_row > 0) state->cursor_row--;
            break;
        case KEY_DOWN:  case 'j':
            if (state->cursor_row < 7) state->cursor_row++;
            break;
        case KEY_LEFT:  case 'h':
            if (state->cursor_col > 0) state->cursor_col--;
            break;
        case KEY_RIGHT: case 'l':
            if (state->cursor_col < 7) state->cursor_col++;
            break;

        case '\n': case '\r': case KEY_ENTER:
            /* Mirrors handle_command(): without this the cursor could
             * move the engine's own pieces (it is their turn, so they
             * read as friendly) out from under the search. */
            if (state->search_running) {
                snprintf(state->status, sizeof(state->status),
                         "Engine is thinking — please wait, or use 'stop'");
            } else if (!state->game.game_over) {
                cursor_enter(state);
            }
            break;

        case 'u':   /* takeback */
            tui_undo(state);
            break;

        case 27: /* Esc */
            clear_selection(state);
            snprintf(state->status, sizeof(state->status), "Deselected.");
            break;

        case -2: /* read_key() signals a completed command line this way */
            if (cmd_buf[0]) {
                if (handle_command(state, cmd_buf) == -1) return 0;
                /* game_over is picked up at the top of the next loop */
                game_update_status(&state->game);
            }
            break;

        default:
            break;
    }
    return 1;
}

void tui_run(TUIState *state)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    init_colors(state->theme);

    if (state->show_onboarding && !tui_onboarding(state)) {
        endwin();
        return; /* the player quit from the onboarding screen */
    }
    init_colors(state->theme); /* re-apply the theme actually confirmed */

    keypad(stdscr, TRUE);
    while (screen_too_small()) {
        screen_draw_too_small();
        wgetch(stdscr);
    }

    Screen sc = screen_create(state);

    /* So "Engine thinking..." appears immediately. */
    state->request_redraw = screen_paint_hook;
    state->redraw_ctx     = &sc;

    /* Engine moves first if it already has the move. */
    if (!state->two_player && state->engine_side == state->game.pos.side)
        handle_command(state, "go");

    char cmd_buf[256];

    for (;;) {

        if (poll_engine_search(state))
            game_update_status(&state->game);

        if (state->game.game_over && state->game.result[0]) {
            screen_paint(&sc);   /* show the final position behind the popup */
            show_game_over_popup(&sc, state);
            state->game.result[0] = '\0';
        }

        screen_paint(&sc);

        int ch = read_key(sc.cmd, cmd_buf, sizeof(cmd_buf), &state->insert_mode);

        if (ch == KEY_RESIZE) {
            screen_handle_resize(&sc);
            continue;
        }

        if (ch == '\t') {   /* stats popup over the board */
            stats_load(&state->stats);
            draw_stats_mini(sc.board, &state->stats);
            continue;        /* the next screen_paint() repaints underneath */
        }

        if (!handle_key(&sc, ch, cmd_buf)) break;
    }

    /* Joining is not optional: the worker writes into TUIState, which
     * lives in main()'s stack frame. Near-instant, since cancellation is
     * checked every ~512 nodes. */
    cancel_engine_search(state);

    screen_destroy(&sc);
    tui_cleanup();
}
