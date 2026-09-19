#include "tui/commands.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/search.h"
#include "engine/move.h"
#include "engine/hash.h"
#include "engine/fen.h"
#include "tui/render.h"
#include "utils/theme.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

/* ── Shared game-lifecycle helpers ──────────────────────────────────────── */

const char *difficulty_label(int difficulty)
{
    return (difficulty == DIFF_EASY) ? "Easy" :
           (difficulty == DIFF_HARD) ? "Hard" : "Medium";
}

/* The "You play White | Medium difficulty" blurb, which three separate
 * places used to format by hand off two ternary chains each. */
void describe_setup(const TUIState *state, char *buf, size_t n)
{
    snprintf(buf, n, "You play %s | %s difficulty",
             (state->player_side == WHITE) ? "White" : "Black",
             difficulty_label(state->difficulty));
}

void tui_undo(TUIState *state)
{
    /* Whatever the engine is thinking about is about to stop being the
     * position on the board. */
    cancel_engine_search(state);

    /* "flip" can set engine_side to -1, which means nobody is playing the
     * other side -- same situation as two-player. */
    int has_engine = (!state->two_player && state->engine_side >= 0);

    /* How many plies it takes to hand the turn back to the human. With an
     * engine that is two when it has already replied and one when it has
     * not; with two humans a takeback is always a single ply. */
    int needed = 1;
    if (has_engine)
        needed = (state->game.pos.side == state->engine_side) ? 1 : 2;

    /* Decided up front rather than undone-then-checked: stopping halfway
     * would leave the engine to move with no search running, and the
     * board would just sit there. Undoing the engine's opening move is
     * the case that hits this. */
    if (state->game.undo_count < needed) {
        snprintf(state->status, sizeof(state->status), "Nothing to undo");
        return;
    }

    for (int i = 0; i < needed; i++)
        game_undo(&state->game);

    state->selected = 0;
    memset(state->highlight, 0, sizeof(state->highlight));
    snprintf(state->status, sizeof(state->status),
             "Took back %d %s", needed, needed == 1 ? "move" : "moves");
}

void tui_new_game(TUIState *state)
{
    /* Any search still running belongs to the game being thrown away;
     * its result must never land on the new board. */
    cancel_engine_search(state);

    game_reset(&state->game);

    state->selected  = 0;
    state->view_side = WHITE;
    memset(state->highlight, 0, sizeof(state->highlight));
    snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");

    char setup[128];
    describe_setup(state, setup, sizeof(setup));
    snprintf(state->status, sizeof(state->status), "New game – %s", setup);
}

static int try_move(TUIState *state, const char *movestr)
{
    int from, to, promo;
    if (!parse_move_str(movestr, &from, &to, &promo)) {
        snprintf(state->status, sizeof(state->status), "Bad move format: %s", movestr);
        return 0;
    }

    Move m;
    if (!game_find_move(&state->game, from, to, promo, &m)) {
        snprintf(state->status, sizeof(state->status), "Illegal move: %s", movestr);
        return 0;
    }

    char text[8];
    move_to_str(m, text);
    game_play(&state->game, m);
    snprintf(state->status, sizeof(state->status), "Played: %s", text);
    return 1;
}

static void apply_engine_result(TUIState *state, SearchResult res);

/* Runs on a separate thread (see start_engine_search()). Searches only
 * state->search_snapshot -- a private copy taken at kickoff time -- so
 * this never touches state->game.pos, and the main thread can keep safely
 * reading/rendering state->game.pos the whole time this runs. */
static void *engine_search_worker(void *arg) {
    TUIState *state = (TUIState *)arg;
    SearchResult res = search(&state->search_snapshot,
                               state->search_depth_arg, state->search_time_limit_arg);

    pthread_mutex_lock(&state->search_mutex);
    state->search_result = res;
    state->search_ready  = 1;
    pthread_mutex_unlock(&state->search_mutex);

    return NULL;
}

/* Kicks off a background search and returns immediately -- it does not
 * wait for a result. Call poll_engine_search() (from the main loop) to
 * notice when it finishes and apply the move. A no-op if a search is
 * already running, so it's safe to call this defensively. */
static void start_engine_search(TUIState *state) {
    if (state->search_running) return;

    /* engine_depth/time_limit_ms are captured into the search itself via
     * arguments inside the worker below; search_snapshot is captured
     * here, before the thread starts, so nothing the worker reads can
     * change out from under it once it's running. */
    state->search_snapshot      = state->game.pos;
    state->search_snapshot_hash = game_hash(&state->game);
    state->search_depth_arg     = state->engine_depth;
    state->search_time_limit_arg = state->time_limit_ms;
    state->search_ready         = 0;
    state->search_running       = 1;

    snprintf(state->status, sizeof(state->status), "Engine thinking...");
    if (state->request_redraw) state->request_redraw(state->redraw_ctx);

    if (pthread_create(&state->search_thread, NULL, engine_search_worker, state) != 0) {
        /* Thread creation failing is rare (resource exhaustion) but not
         * impossible -- fall back to a synchronous search rather than
         * leaving search_running stuck true forever with nothing ever
         * going to clear it.
         *
         * This path must apply the result itself: with search_running
         * back to 0, poll_engine_search() bails out at its first line
         * and would never look at search_ready, so leaving the result
         * sitting in the struct means the engine simply never moves. */
        state->search_running = 0;
        state->search_ready   = 0;
        SearchResult res = search(&state->game.pos, state->engine_depth, state->time_limit_ms);
        apply_engine_result(state, res);
    }
}

/* Applies a completed search result: plays the move (or ends the game),
 * updates eval history and the status line. Shared by poll_engine_search()
 * (the normal, threaded path) and start_engine_search()'s same-thread
 * fallback above. */
static void apply_engine_result(TUIState *state, SearchResult res)
{
    if (!res.best_move) {
        /* An empty result means the search found nothing to play -- which
         * is only "game over" if the position genuinely has no legal
         * move. A search cancelled during depth 1 also comes back empty
         * (see search.h), and calling that checkmate would end a live
         * game on a bogus result. Ask the board, not the search. */
        if (has_legal_moves(&state->game.pos)) {
            snprintf(state->status, sizeof(state->status),
                     "Search stopped — no move played");
            return;
        }
        game_update_status(&state->game);
        snprintf(state->status, sizeof(state->status), "%s", state->game.result);
        return;
    }

    /* Normalize the score to White's perspective: negamax reports it for
     * the side that just moved, so if the engine is Black a positive
     * score means Black is ahead -- flip it so last_eval always reads
     * from White's point of view. */
    int score_white = (state->game.pos.side == BLACK) ? res.best_score : -res.best_score;
    float eval_f = score_white / 100.0f;
    snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", eval_f);
    game_record_eval(&state->game, res.best_score);

    char text[8];
    move_to_str(res.best_move, text);
    game_play(&state->game, res.best_move);

    snprintf(state->status, sizeof(state->status), "Engine: %s (eval %+.2f, depth %d)",
             text, eval_f, res.depth_reached);
}

/* Is a just-finished search still about the position on screen? */
static int engine_result_is_current(const TUIState *state)
{
    if (state->game.game_over) return 0;
    return game_hash(&state->game) == state->search_snapshot_hash;
}

int poll_engine_search(TUIState *state) {
    if (!state->search_running) return 0;

    pthread_mutex_lock(&state->search_mutex);
    int ready = state->search_ready;
    SearchResult res = state->search_result;
    pthread_mutex_unlock(&state->search_mutex);

    if (!ready) return 0;

    pthread_join(state->search_thread, NULL);
    state->search_running = 0;
    state->search_ready   = 0;

    /* Only play the move if the board is still the one it was computed
     * for. Anything that changed `pos` while the search was in flight
     * (a piece moved via the board cursor, a new game started from the
     * game-over popup) invalidates the result -- applying it anyway
     * would move a piece that has since left its square. */
    if (!engine_result_is_current(state)) {
        /* Discarding a result must not leave the engine owing a move it
         * will never play, so re-search the position that is actually on
         * the board. The fresh snapshot matches it by construction, so
         * this cannot loop. */
        if (!state->game.game_over && !state->two_player &&
            state->engine_side == state->game.pos.side)
            start_engine_search(state);
        return 0;
    }

    apply_engine_result(state, res);
    return 1;
}

/* See commands.h. Use this, not a "please wait" rejection, for anything
 * that means "start over." */
void cancel_engine_search(TUIState *state) {
    if (!state->search_running) return;
    search_cancel();
    pthread_join(state->search_thread, NULL);
    state->search_running = 0;
    state->search_ready    = 0;
}

int handle_command(TUIState *state, const char *cmd) {
    if (!cmd || !cmd[0]) return 1;

    /* "stop": ask an in-flight search to return its best-guess-so-far
     * move right now, same idea as UCI's "stop" -- the last cleanly
     * completed iteration always has a legal move ready (see search.c),
     * so this always has something to apply, never nothing. */
    if (strcmp(cmd, "stop") == 0) {
        if (!state->search_running) {
            snprintf(state->status, sizeof(state->status), "No search in progress");
            return 1;
        }
        search_cancel();
        pthread_join(state->search_thread, NULL);
        state->search_running = 0;
        pthread_mutex_lock(&state->search_mutex);
        SearchResult res = state->search_result;
        state->search_ready = 0;
        pthread_mutex_unlock(&state->search_mutex);
        apply_engine_result(state, res);
        return 1;
    }

    /* Commands that don't touch state->game.pos and wouldn't call search()
     * again concurrently (fen, theme, help, stats, quit) are always
     * safe to run immediately, search or no search.
     *
     * Moves/"go"/"eval" are simply rejected while the engine thinks --
     * none of them mean "start over," so waiting (or using "stop" first)
     * makes more sense than force-cancelling on their behalf. */
    int is_move = (cmd[0] >= 'a' && cmd[0] <= 'h' && cmd[1] >= '1' && cmd[1] <= '8');
    int reject_while_thinking =
        is_move ||
        strcmp(cmd, "go") == 0 ||
        strcmp(cmd, "eval") == 0;

    if (reject_while_thinking && state->search_running) {
        snprintf(state->status, sizeof(state->status),
                 "Engine is thinking — please wait, or use 'stop'");
        return 1;
    }

    /* "new"/"loadfen"/"flip" all mean "replace the current game state,"
     * so a stale in-flight search for the position being replaced isn't
     * worth waiting for -- cancel it (discarding whatever it had found;
     * see cancel_engine_search()) and proceed immediately instead of
     * making the player wait or rejecting the command outright. */
    if (strcmp(cmd, "new") == 0 || strcmp(cmd, "flip") == 0 ||
        strcmp(cmd, "undo") == 0 || strcmp(cmd, "u") == 0 ||
        strncmp(cmd, "loadfen ", 8) == 0) {
        cancel_engine_search(state);
    }

    if (is_move) {
        if (!state->game.game_over && try_move(state, cmd))
            if (state->engine_side == state->game.pos.side && !state->game.game_over)
                start_engine_search(state);
        return 1;
    }
    if (strcmp(cmd, "go") == 0) {
        if (!state->game.game_over) start_engine_search(state);
        return 1;
    }
    if (strncmp(cmd, "depth ", 6) == 0) {
        int d = atoi(cmd + 6);
        if (d >= 1 && d <= 8) {
            state->engine_depth = d;
            snprintf(state->status, sizeof(state->status),
                     "Depth cap set to %d (still bounded by the %.1fs time budget)",
                     d, state->time_limit_ms / 1000.0f);
        }
        return 1;
    }
    if (strcmp(cmd, "undo") == 0 || strcmp(cmd, "u") == 0) {
        tui_undo(state);
        return 1;
    }
    if (strcmp(cmd, "new") == 0) {
        tui_new_game(state);

        /* If it is already the engine's turn (the player chose Black),
         * it moves first. */
        if (!state->two_player && state->engine_side == state->game.pos.side)
            start_engine_search(state);
        return 1;
    }
    if (strcmp(cmd, "fen") == 0) {
        char buf[FEN_BUFSIZE];
        int fullmove = state->game.move_count / 2 + 1;
        position_to_fen(&state->game.pos, state->game.halfmove_clock, fullmove, buf, sizeof(buf));
        snprintf(state->status, sizeof(state->status), "FEN: %s", buf);
        return 1;
    }
    if (strncmp(cmd, "loadfen ", 8) == 0) {
        if (!game_load_fen(&state->game, cmd + 8)) {
            snprintf(state->status, sizeof(state->status),
                     "Invalid FEN, position unchanged");
            return 1;
        }
        state->selected = 0;
        memset(state->highlight, 0, sizeof(state->highlight));
        snprintf(state->status, sizeof(state->status), "Position loaded from FEN");

        if (!state->two_player && state->engine_side == state->game.pos.side)
            start_engine_search(state);
        return 1;
    }
    if (strncmp(cmd, "theme ", 6) == 0) {
        int t = theme_from_name(cmd + 6);
        if (t < 0) {
            snprintf(state->status, sizeof(state->status),
                     "Unknown theme '%s'. Use: classic | midnight | forest | contrast",
                     cmd + 6);
            return 1;
        }
        state->theme = t;
        init_colors(t);
        if (state->request_redraw) state->request_redraw(state->redraw_ctx);
        snprintf(state->status, sizeof(state->status), "Theme: %s", theme_name(t));
        return 1;
    }
    if (strcmp(cmd, "flip") == 0) {
        if      (state->engine_side == BLACK)  state->engine_side = WHITE;
        else if (state->engine_side == WHITE)  state->engine_side = -1;
        else                                   state->engine_side = BLACK;
        char *s = state->engine_side == WHITE ? "White" :
                  state->engine_side == BLACK ? "Black" : "None";
        snprintf(state->status, sizeof(state->status), "Engine plays: %s", s);
        return 1;
    }
    if (strcmp(cmd, "eval") == 0) {
        SearchResult res = search(&state->game.pos, 1, 0);
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", res.best_score/100.0f);
        snprintf(state->status, sizeof(state->status), "Eval: %s", state->last_eval);
        return 1;
    }
    if (strcmp(cmd, "stats") == 0) {
        /* Reload latest stats from disk */
        stats_load(&state->stats);
        int total = state->stats.games_played[0] +
                    state->stats.games_played[1] +
                    state->stats.games_played[2];
        int wins  = state->stats.wins[0] +
                    state->stats.wins[1] +
                    state->stats.wins[2];
        snprintf(state->status, sizeof(state->status),
                 "Stats: %d games, %d wins (%.0f%%) | run dchess --stats for full view",
                 total, wins,
                 total ? 100.0f * wins / total : 0.0f);
        return 1;
    }
    if (strcmp(cmd, "help") == 0) {
        snprintf(state->status, sizeof(state->status),
                 "e2e4|go|stop|undo|new|flip|depth N|eval|fen|loadfen <FEN>|stats|quit  (dchess --help for full docs)");
        return 1;
    }
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) return -1;

    snprintf(state->status, sizeof(state->status), "Unknown: '%s' (type 'help')", cmd);
    return 1;
}
