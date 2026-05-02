#include "tui/commands.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/search.h"
#include "engine/move.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Record a move + clock + halfmove into state (shared by try_move and engine_move) */
static void record_move(TUIState *state, Move m, int piece, const char *buf)
{
    int to_sq = TO(m);

    /* Clock */
    time_t now = time(NULL);
    int elapsed = 0;
    if (state->clock_started) {
        elapsed = (int)(now - state->turn_start);
        if (state->pos.side == WHITE) state->white_clock += elapsed;
        else                          state->black_clock += elapsed;
    }
    state->clock_started = 1;
    state->turn_start = now;
    clock_gettime(CLOCK_MONOTONIC, &state->turn_start_mono);

    /* Halfmove clock */
    int is_pawn    = (piece == 0 || piece == 6);
    int is_capture = (to_sq >= 0 && to_sq < 64 &&
                      GET_BIT(state->pos.occupancies[BOTH], to_sq)) ? 1 : 0;
    if (is_pawn || is_capture) state->halfmove_clock = 0;
    else                       state->halfmove_clock++;

    /* Make the move */
    make_move(&state->pos, m);

    /* Position history for repetition */
    if (state->pos_history_count < MAX_MOVE_HISTORY) {
        /* Simple hash inline */
        U64 h = 0xcbf29ce484222325ULL;
        for (int i = 0; i < 12; i++) {
            h ^= state->pos.bitboards[i] * 0x9e3779b97f4a7c15ULL;
            h += (h << 6) + (h >> 2);
        }
        h ^= (U64)state->pos.side     * 0x517cc1b727220a95ULL;
        h ^= (U64)state->pos.castling * 0x6c62272e07bb0142ULL;
        h ^= (U64)(state->pos.enpassant+1) * 0xbf58476d1ce4e5b9ULL;
        state->pos_history[state->pos_history_count++] = h;
    }

    /* Move history */
    int idx = state->move_count;
    if (idx < MAX_MOVE_HISTORY) {
        strncpy(state->move_history[idx], buf, 7);
        state->move_history[idx][7] = '\0';
        state->move_piece[idx] = piece;
        state->move_time[idx]  = elapsed;
        state->move_count++;
    }
}

static int try_move(TUIState *state, const char *movestr) {
    int from, to, promo;
    if (!parse_move_str(movestr, &from, &to, &promo)) {
        snprintf(state->status, sizeof(state->status), "Bad move format: %s", movestr);
        return 0;
    }

    MoveList ml;
    generate_moves(&state->pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;
        if (promo) { if (!(FLAGS(m) & promo)) continue; }
        else if (FLAGS(m) & FLAG_PROMOTION) { if (!(FLAGS(m) & FLAG_PROMO_Q)) continue; }

        int piece = -1;
        for (int j = 0; j < 12; j++)
            if (GET_BIT(state->pos.bitboards[j], from)) { piece = j; break; }

        Position saved;
        memcpy(&saved, &state->pos, sizeof(Position));
        Position test = saved;
        if (!make_move(&test, m)) {
            snprintf(state->status, sizeof(state->status), "Illegal: leaves king in check");
            return 0;
        }

        char buf[8];
        move_to_str(m, buf);
        record_move(state, m, piece, buf);
        snprintf(state->status, sizeof(state->status), "Played: %s", buf);
        return 1;
    }

    snprintf(state->status, sizeof(state->status), "Illegal move: %s", movestr);
    return 0;
}

static void engine_move(TUIState *state) {
    snprintf(state->status, sizeof(state->status), "Engine thinking...");
    SearchResult res = search(&state->pos, state->engine_depth);

    if (!res.best_move) {
        state->game_over = 1;
        if (is_in_check(&state->pos, state->pos.side)) {
            const char *w = state->pos.side == WHITE ? "Black" : "White";
            snprintf(state->game_result, sizeof(state->game_result),
                     "Checkmate — %s wins!", w);
        } else {
            snprintf(state->game_result, sizeof(state->game_result), "Stalemate — Draw!");
        }
        snprintf(state->status, sizeof(state->status), "%s", state->game_result);
        return;
    }

    int from_sq = FROM(res.best_move);
    int piece = -1;
    for (int j = 0; j < 12; j++)
        if (GET_BIT(state->pos.bitboards[j], from_sq)) { piece = j; break; }

    char buf[8];
    move_to_str(res.best_move, buf);

    /* Normalize score to White's perspective:
     * negamax returns score for the side that just moved.
     * If engine plays Black, a positive score means Black is ahead —
     * flip sign so last_eval is always from White's point of view. */
    int score_white = (state->pos.side == BLACK) ? res.best_score : -res.best_score;
    float eval_f = score_white / 100.0f;
    snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", eval_f);

    /* Record eval history */
    if (state->eval_count < MAX_MOVE_HISTORY)
        state->eval_history[state->eval_count++] = res.best_score;

    record_move(state, res.best_move, piece, buf);
    snprintf(state->status, sizeof(state->status), "Engine: %s (eval %+.2f)", buf, eval_f);
}

int handle_command(TUIState *state, const char *cmd) {
    if (!cmd || !cmd[0]) return 1;

    if (cmd[0] >= 'a' && cmd[0] <= 'h' && cmd[1] >= '1' && cmd[1] <= '8') {
        if (!state->game_over && try_move(state, cmd))
            if (state->engine_side == state->pos.side && !state->game_over)
                engine_move(state);
        return 1;
    }
    if (strcmp(cmd, "go") == 0) {
        if (!state->game_over) engine_move(state);
        return 1;
    }
    if (strncmp(cmd, "depth ", 6) == 0) {
        int d = atoi(cmd + 6);
        if (d >= 1 && d <= 8) {
            state->engine_depth = d;
            snprintf(state->status, sizeof(state->status), "Depth set to %d", d);
        }
        return 1;
    }
    if (strcmp(cmd, "new") == 0) {
        init_start_position(&state->pos);
        state->move_count        = 0;
        state->game_over         = 0;
        state->game_result[0]    = '\0';
        state->turn_start        = time(NULL);
        clock_gettime(CLOCK_MONOTONIC, &state->turn_start_mono);
        state->white_clock       = 0;
        state->black_clock       = 0;
        state->halfmove_clock    = 0;
        state->pos_history_count = 0;
        state->selected          = 0;
        state->clock_started     = 0;
        memset(state->highlight, 0, sizeof(state->highlight));
        state->view_side = WHITE;

        const char *diff_str = (state->difficulty == DIFF_EASY)   ? "Easy"   :
                               (state->difficulty == DIFF_HARD)   ? "Hard"   : "Medium";
        const char *side_str = (state->player_side == WHITE) ? "White" : "Black";
        snprintf(state->status, sizeof(state->status),
                 "New game – You play %s | %s difficulty", side_str, diff_str);

        /* If player is Black, engine (White) moves first */
        if (state->engine_side == WHITE)
            engine_move(state);
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
        SearchResult res = search(&state->pos, 1);
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
                 "e2e4|go|new|flip|depth N|eval|stats|quit  (dchess --help for full docs)");
        return 1;
    }
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) return -1;

    snprintf(state->status, sizeof(state->status), "Unknown: '%s' (type 'help')", cmd);
    return 1;
}
