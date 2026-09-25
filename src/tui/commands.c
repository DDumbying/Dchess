#include "tui/commands.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/search.h"
#include "engine/move.h"
#include "engine/hash.h"
#include "engine/fen.h"
#include "game/pgn.h"
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

const char *difficulty_label(int difficulty)
{
    return (difficulty == DIFF_EASY) ? "Easy" :
           (difficulty == DIFF_HARD) ? "Hard" : "Medium";
}

void describe_setup(const TUIState *state, char *buf, size_t n)
{
    snprintf(buf, n, "You play %s | %s difficulty",
             (state->player_side == WHITE) ? "White" : "Black",
             difficulty_label(state->difficulty));
}

void tui_undo(TUIState *state)
{
    cancel_engine_search(state);

    /* "flip" can set engine_side to -1: nobody plays the other side. */
    int has_engine = (!state->two_player && state->engine_side >= 0);

    /* Plies needed to hand the turn back to the human. */
    int needed = 1;
    if (has_engine)
        needed = (state->game.pos.side == state->engine_side) ? 1 : 2;

    /* Decided up front: stopping halfway would leave the engine to move
     * with no search running, and the board would just sit there. */
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

    game_play(&state->game, m);
    snprintf(state->status, sizeof(state->status), "Played: %s",
             state->game.move_history[state->game.move_count - 1]);
    return 1;
}

static void apply_engine_result(TUIState *state, SearchResult res);

/* Runs on the worker thread. Touches only search_snapshot. */
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

/* Returns immediately; poll_engine_search() applies the result. No-op if
 * a search is already running. */
static void start_engine_search(TUIState *state) {
    if (state->search_running) return;

    /* Captured before the thread starts, so nothing it reads can change
     * under it. */
    state->search_snapshot      = state->game.pos;
    state->search_snapshot_hash = game_hash(&state->game);
    state->search_depth_arg     = state->engine_depth;
    state->search_time_limit_arg = state->time_limit_ms;
    state->search_ready         = 0;
    state->search_running       = 1;

    snprintf(state->status, sizeof(state->status), "Engine thinking...");
    if (state->request_redraw) state->request_redraw(state->redraw_ctx);

    if (pthread_create(&state->search_thread, NULL, engine_search_worker, state) != 0) {
        /* Rare, but must not leave search_running stuck true. This path
         * applies the result itself: with search_running back to 0,
         * poll_engine_search() would never look at it. */
        state->search_running = 0;
        state->search_ready   = 0;
        SearchResult res = search(&state->game.pos, state->engine_depth, state->time_limit_ms);
        apply_engine_result(state, res);
    }
}


static void apply_engine_result(TUIState *state, SearchResult res)
{
    if (!res.best_move) {
        /* A cancelled search also comes back empty (see search.h), so ask
         * the board whether the game is really over. */
        if (has_legal_moves(&state->game.pos)) {
            snprintf(state->status, sizeof(state->status),
                     "Search stopped — no move played");
            return;
        }
        game_update_status(&state->game);
        snprintf(state->status, sizeof(state->status), "%s", state->game.result);
        return;
    }

    int score_white = eval_white_view(res.best_score, state->game.pos.side);
    float eval_f = score_white / 100.0f;
    snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", eval_f);
    game_record_eval(&state->game, score_white);

    game_play(&state->game, res.best_move);

    snprintf(state->status, sizeof(state->status), "Engine: %s (eval %+.2f, depth %d)",
             state->game.move_history[state->game.move_count - 1],
             eval_f, res.depth_reached);
}


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

    /* Applying a result computed for a different board would move a
     * piece that has since left its square. */
    if (!engine_result_is_current(state)) {
        /* Re-search the position actually on the board, so the engine is
         * not left owing a move. The fresh snapshot matches by
         * construction, so this cannot loop. */
        if (!state->game.game_over && !state->two_player &&
            state->engine_side == state->game.pos.side)
            start_engine_search(state);
        return 0;
    }

    apply_engine_result(state, res);
    return 1;
}


void cancel_engine_search(TUIState *state) {
    if (!state->search_running) return;
    search_cancel();
    pthread_join(state->search_thread, NULL);
    state->search_running = 0;
    state->search_ready    = 0;
}

int handle_command(TUIState *state, const char *cmd) {
    if (!cmd || !cmd[0]) return 1;

    /* Like UCI's "stop": return the best move found so far. */
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

    /* Moves/"go"/"eval" are rejected while the engine thinks; none of
     * them mean "start over". Everything else is safe to run. */
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

    /* These replace the game state, so a search for the old position is
     * not worth waiting for. */
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


        if (!state->two_player && state->engine_side == state->game.pos.side)
            start_engine_search(state);
        return 1;
    }
    if (strcmp(cmd, "pgn") == 0 || strncmp(cmd, "pgn ", 4) == 0) {
        char path[512];
        if (cmd[3] == ' ' && cmd[4])
            snprintf(path, sizeof(path), "%s", cmd + 4);
        else
            pgn_default_path(path, sizeof(path));

        char engine_name[64];
        snprintf(engine_name, sizeof(engine_name), "dchess (%s)",
                 difficulty_label(state->difficulty));

        PgnHeader h = {
            .event = "Casual game",
            .site  = "dchess",
            .white = (state->player_side == WHITE) ? "Player" : engine_name,
            .black = (state->player_side == WHITE) ? engine_name : "Player",
        };
        if (state->two_player) { h.white = "Player 1"; h.black = "Player 2"; }

        if (pgn_write(&state->game, &h, path) == 0)
            snprintf(state->status, sizeof(state->status), "Saved PGN: %.200s", path);
        else
            snprintf(state->status, sizeof(state->status),
                     "Could not write PGN to %.200s", path);
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
            char names[96] = "";
            for (int i = 0; i < theme_count(); i++) {
                strncat(names, theme_name(i), sizeof(names) - strlen(names) - 1);
                if (i + 1 < theme_count())
                    strncat(names, " | ", sizeof(names) - strlen(names) - 1);
            }
            snprintf(state->status, sizeof(state->status),
                     "Unknown theme '%.40s'. Use: %s", cmd + 6, names);
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
        int score_white = eval_white_view(res.best_score, state->game.pos.side);
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", score_white / 100.0f);
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
                 "e2e4|go|stop|undo|new|flip|depth N|eval|fen|pgn|loadfen <FEN>|stats|quit  (dchess --help for full docs)");
        return 1;
    }
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) return -1;

    snprintf(state->status, sizeof(state->status), "Unknown: '%s' (type 'help')", cmd);
    return 1;
}
