#include "tui/commands.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/search.h"
#include "engine/move.h"
#include "utils/constants.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int try_move(TUIState *state, const char *movestr) {
    int from, to, promo;
    if (!parse_move_str(movestr, &from, &to, &promo)) {
        snprintf(state->status, sizeof(state->status), "Bad move format: %s", movestr);
        return 0;
    }

    /* Find matching legal move */
    MoveList ml;
    generate_moves(&state->pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;
        /* Check promo match */
        if (promo) {
            if (!(FLAGS(m) & promo)) continue;
        } else if (FLAGS(m) & FLAG_PROMOTION) {
            /* Default to queen */
            if (!(FLAGS(m) & FLAG_PROMO_Q)) continue;
        }

        Position saved;
        memcpy(&saved, &state->pos, sizeof(Position));
        if (!make_move(&state->pos, m)) {
            memcpy(&state->pos, &saved, sizeof(Position));
            snprintf(state->status, sizeof(state->status), "Illegal: leaves king in check");
            return 0;
        }

        /* Record */
        char buf[8];
        move_to_str(m, buf);
        if (state->move_count < MAX_MOVE_HISTORY)
            strncpy(state->move_history[state->move_count++], buf, 7);

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
        if (is_in_check(&state->pos, state->pos.side))
            snprintf(state->status, sizeof(state->status), "Checkmate! %s wins.",
                state->pos.side == WHITE ? "Black" : "White");
        else
            snprintf(state->status, sizeof(state->status), "Stalemate! Draw.");
        state->game_over = 1;
        return;
    }

    char buf[8];
    move_to_str(res.best_move, buf);
    make_move(&state->pos, res.best_move);
    if (state->move_count < MAX_MOVE_HISTORY)
        strncpy(state->move_history[state->move_count++], buf, 7);

    float eval_f = res.best_score / 100.0f;
    snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", eval_f);
    snprintf(state->status, sizeof(state->status), "Engine: %s (eval %+.2f)", buf, eval_f);
}

int handle_command(TUIState *state, const char *cmd) {
    if (!cmd || !cmd[0]) return 1;

    /* Move command: e2e4, e7e8q, etc. */
    if (cmd[0] >= 'a' && cmd[0] <= 'h' && cmd[1] >= '1' && cmd[1] <= '8') {
        if (!state->game_over && try_move(state, cmd)) {
            /* If engine is playing the other side, trigger engine move */
            if (state->engine_side == state->pos.side && !state->game_over) {
                engine_move(state);
            }
        }
        return 1;
    }

    /* go: engine makes a move */
    if (strcmp(cmd, "go") == 0) {
        if (!state->game_over) engine_move(state);
        return 1;
    }

    /* depth N */
    if (strncmp(cmd, "depth ", 6) == 0) {
        int d = atoi(cmd + 6);
        if (d >= 1 && d <= 8) {
            state->engine_depth = d;
            snprintf(state->status, sizeof(state->status), "Depth set to %d", d);
        }
        return 1;
    }

    /* new: reset game */
    if (strcmp(cmd, "new") == 0) {
        init_start_position(&state->pos);
        state->move_count = 0;
        state->game_over  = 0;
        state->status[0]  = '\0';
        snprintf(state->status, sizeof(state->status), "New game started");
        return 1;
    }

    /* flip: switch engine side */
    if (strcmp(cmd, "flip") == 0) {
        if (state->engine_side == BLACK)      state->engine_side = WHITE;
        else if (state->engine_side == WHITE) state->engine_side = -1;
        else                                  state->engine_side = BLACK;
        char *s = state->engine_side == WHITE ? "White" :
                  state->engine_side == BLACK ? "Black" : "None";
        snprintf(state->status, sizeof(state->status), "Engine plays: %s", s);
        return 1;
    }

    /* eval: show current evaluation */
    if (strcmp(cmd, "eval") == 0) {
        SearchResult res = search(&state->pos, 1);
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", res.best_score/100.0f);
        snprintf(state->status, sizeof(state->status), "Eval: %s", state->last_eval);
        return 1;
    }

    /* help */
    if (strcmp(cmd, "help") == 0) {
        snprintf(state->status, sizeof(state->status), "e2e4|go|new|flip|depth N|eval|quit");
        return 1;
    }

    /* quit */
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
        return -1; /* signal quit */
    }

    snprintf(state->status, sizeof(state->status), "Unknown: '%s' (type 'help')", cmd);
    return 1;
}
