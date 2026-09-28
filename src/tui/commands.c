#include "tui/commands.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/search.h"
#include "engine/move.h"
#include "engine/hash.h"
#include "engine/fen.h"
#include "game/pgn.h"
#include "game/uci.h"
#include "game/records.h"
#include "game/book.h"
#include "tui/render.h"
#include "tui/stats_tui.h"
#include "utils/theme.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ANALYSIS_MS 1500   /* per position while analysing */

static int analysis_wanted(const char *engine)
{
    return engine[0] && strcmp(engine, "off") != 0;
}


static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

void describe_setup(const TUIState *state, char *buf, size_t n)
{
    players_describe(state->players, buf, n);
}

void tui_remember_setup(TUIState *state)
{
    if (!state->profiles.count) return;
    Profile *p = &state->profiles.p[state->profiles.active];
    if (state->theme_set)
        snprintf(p->theme, sizeof(p->theme), "%s", theme_name(state->theme));
    player_word(&state->players[WHITE], p->white, sizeof(p->white));
    player_word(&state->players[BLACK], p->black, sizeof(p->black));
    if (!state->cli_book[0])   /* --book is for one run */
        snprintf(p->book, sizeof(p->book), "%s", state->book_choice);
    if (!state->cli_clock[0]) {   /* --clock is for one run */
        if (tc_timed(&state->game.tc)) tc_format(&state->game.tc, p->clock, sizeof(p->clock));
        else p->clock[0] = '\0';
    }
    snprintf(p->analysis, sizeof(p->analysis), "%s",
             analysis_wanted(state->analysis_engine) ? state->analysis_engine : "");
    /* Saved with the file's own active profile: --profile is for one run. */
    int run = state->profiles.active;
    state->profiles.active = state->file_active;
    profiles_save(&state->profiles);
    state->profiles.active = run;
}

/* The engines' pause and a timed game's clocks go together. */
static void set_paused(TUIState *s, int on)
{
    s->paused = on;
    if (!tc_timed(&s->game.tc)) return;
    if (on) game_clock_pause(&s->game);
    else    game_clock_resume(&s->game);
}

void cancel_engine_search(TUIState *state)
{
    if (!state->thinking) return;
    opponent_cancel(state->thinking);
    state->thinking = NULL;
}

static void attach(TUIState *state, int side)
{
    if (state->drivers[side] && state->thinking == state->drivers[side])
        cancel_engine_search(state);
    opponent_free(state->drivers[side]);
    state->drivers[side] = NULL;

    const Player *p = &state->players[side];
    if (p->kind == PLAYER_BUILTIN) {
        state->drivers[side] = opponent_builtin(p->depth, p->time_ms, state->book, p->level);
    } else if (p->kind == PLAYER_UCI) {
        const EngineEntry *e = engines_find(&state->engines, p->name);
        if (e) state->drivers[side] = opponent_uci(e);
    }
}

/* A bad path falls back to the built-in book, with a message. */
static void load_book(TUIState *state)
{
    char err[200];
    book_free(state->book);
    state->book = NULL;
    if (strcmp(state->book_choice, "off") == 0) return;
    if (strcmp(state->book_choice, "builtin") != 0) {
        state->book = book_open(state->book_choice, err, sizeof(err));
        if (state->book) return;
        snprintf(state->status, sizeof(state->status), "%s, using the built-in book", err);
        snprintf(state->book_choice, sizeof(state->book_choice), "builtin");
    }
    state->book = book_builtin();
}

void tui_attach_players(TUIState *state)
{
    if (!state->book) load_book(state);
    attach(state, WHITE);
    attach(state, BLACK);
    if (!state->go_driver) {
        Player m = player_builtin(DIFF_MEDIUM);
        state->go_driver = opponent_builtin(m.depth, m.time_ms, state->book, DIFF_MEDIUM);
    }
}

void tui_release_players(TUIState *state)
{
    cancel_engine_search(state);
    for (int side = WHITE; side <= BLACK; side++) {
        opponent_free(state->drivers[side]);
        state->drivers[side] = NULL;
    }
    opponent_free(state->go_driver);
    state->go_driver = NULL;
    book_free(state->book);
    state->book = NULL;
    tui_analysis_free(state);
}

void tui_check_flag(TUIState *state)
{
    if (!game_check_flag(&state->game)) return;
    cancel_engine_search(state);
    snprintf(state->status, sizeof(state->status), "%s", state->game.result);
}

int tui_can_move_by_hand(const TUIState *state)
{
    return state->paused || !players_automated(state->players, state->game.pos.side);
}

static int both_engines(const TUIState *state)
{
    return players_automated(state->players, WHITE) &&
           players_automated(state->players, BLACK);
}

static int same_player(const Player *a, const Player *b)
{
    return a->kind == b->kind && a->level == b->level &&
           a->depth == b->depth && a->time_ms == b->time_ms &&
           strcmp(a->name, b->name) == 0;
}

void tui_undo(TUIState *state)
{
    cancel_engine_search(state);

    int n = players_undo_plies(state->players, state->game.pos.side,
                               state->game.undo_count);
    if (!n) {
        snprintf(state->status, sizeof(state->status), "Nothing to undo");
        return;
    }
    for (int i = 0; i < n; i++)
        game_undo(&state->game);

    /* Otherwise the engine would play the move straight back. */
    if (both_engines(state)) set_paused(state, 1);

    int cp;
    if (game_last_eval(&state->game, &cp))
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", cp / 100.0f);
    else
        snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");

    state->selected = 0;
    memset(state->highlight, 0, sizeof(state->highlight));
    snprintf(state->status, sizeof(state->status), "Took back %d %s%s",
             n, n == 1 ? "move" : "moves", state->paused ? " — paused" : "");
}

void tui_new_game(TUIState *state)
{
    cancel_engine_search(state);

    game_reset(&state->game);
    memset(&state->last_search, 0, sizeof(state->last_search));
    state->last_was_book = 0;
    state->last_search_by[0] = '\0';
    set_paused(state, 0);
    state->engine_error[0] = '\0';

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

static void apply_engine_result(TUIState *state, SearchResult res, const char *by)
{
    if (!res.best_move) {
        /* A stopped search also comes back empty (see search.h), so ask
         * the board whether the game is really over. */
        if (has_legal_moves(&state->game.pos)) {
            /* Or the next tick would start the same search again; only an
             * engine to move would. */
            set_paused(state, players_automated(state->players, state->game.pos.side));
            snprintf(state->status, sizeof(state->status), "%s",
                     state->paused ? "Search stopped before it found a move — paused"
                                   : "Search stopped before it found a move");
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
    state->last_search = res;
    state->last_was_book = res.depth_reached == 0 && res.nodes == 0;
    snprintf(state->last_search_by, sizeof(state->last_search_by), "%s", by);

    game_play(&state->game, res.best_move);
    state->last_move_ms = now_ms();

    if (state->last_was_book)
        snprintf(state->status, sizeof(state->status), "%s: %s (book)",
                 by, state->game.move_history[state->game.move_count - 1]);
    else
        snprintf(state->status, sizeof(state->status), "%s: %s (eval %+.2f, depth %d)",
                 by, state->game.move_history[state->game.move_count - 1],
                 eval_f, res.depth_reached);
}

/* Built-in analysis cannot run beside a built-in search. */
static int analysis_waits(const TUIState *s)
{
    return analyser_is_builtin(s->analyser) && s->thinking && opponent_is_builtin(s->thinking);
}

void tui_analysis_free(TUIState *s)
{
    analyser_free(s->analyser);
    s->analyser = NULL;
    s->analysis_key = 0;
    s->analysis_ready = 0;
}

void tui_analysis_toggle(TUIState *s)
{
    s->analysis_on = !s->analysis_on;
    if (!s->analysis_on) tui_analysis_free(s);
    s->analysis_err[0] = '\0';
    snprintf(s->status, sizeof(s->status), "Analysis %s", s->analysis_on ? "on" : "off");
}

void tui_analysis_tick(TUIState *s)
{
    if (!s->analysis_on) return;
    if (!s->analyser) {
        const char *name = s->analysis_engine;
        if (!analysis_wanted(name) || !strcmp(name, "builtin")) {
            s->analyser = analyser_builtin(ANALYSIS_MS);
        } else {
            const EngineEntry *e = engines_find(&s->engines, name);
            if (!e) {
                snprintf(s->analysis_err, sizeof(s->analysis_err), "no engine named %s", name);
                return;
            }
            s->analyser = analyser_uci(e, ANALYSIS_MS);
        }
        s->analysis_key = 0;
        if (!s->analyser) return;
    }
    Analysis r;
    U64 key, now = game_hash(&s->game);
    if (analyser_poll(s->analyser, &r, &key)) {
        const char *err = analyser_error(s->analyser);
        if (err && err[0]) snprintf(s->analysis_err, sizeof(s->analysis_err), "%s", err);
        else if (key == now) { s->analysis = r; s->analysis_ready = 1; }
    }
    if (now != s->analysis_key) s->analysis_ready = 0;   /* an old position's view */
    if (analysis_waits(s)) {
        s->analysis_blocked = 1;
        return;
    }
    s->analysis_blocked = 0;
    if (now == s->analysis_key) return;
    s->analysis_key = now;
    s->analysis_ready = 0;
    s->analysis_err[0] = '\0';
    analyser_start(s->analyser, &s->game);
}

static void start_thinking(TUIState *state, Opponent *o, const char *by)
{
    if (o && opponent_is_builtin(o) && analyser_is_builtin(state->analyser)) {
        analyser_stop(state->analyser);
        state->analysis_key = 0;       /* restarts once the engine is done */
        state->analysis_ready = 0;
    }
    if (!o) {
        /* Pausing stops the next tick from trying again at once. */
        set_paused(state, 1);
        snprintf(state->status, sizeof(state->status),
                 "Could not start %s — paused", by);
        return;
    }
    if (!opponent_start(o, &state->game)) return;

    state->engine_error[0] = '\0';
    state->thinking = o;
    snprintf(state->thinking_by, sizeof(state->thinking_by), "%s", by);
    snprintf(state->status, sizeof(state->status), "%s thinking...", by);
    if (state->request_redraw) state->request_redraw(state->redraw_ctx);
}

int drive_turn(TUIState *state)
{
    if (state->thinking) {
        SearchResult res;
        U64 key;
        if (!opponent_poll(state->thinking, &res, &key)) return 0;
        const char *err = opponent_error(state->thinking);
        state->thinking = NULL;

        if (err) {
            snprintf(state->engine_error, sizeof(state->engine_error), "%s", err);
            set_paused(state, 1);
            snprintf(state->status, sizeof(state->status), "%s — paused", err);
            return 1;
        }

        /* A result for a board that has since changed would move a piece
         * that is no longer there. The next tick searches again. */
        if (state->game.game_over || key != game_hash(&state->game)) return 0;

        apply_engine_result(state, res, state->thinking_by);
        return 1;
    }

    int side = state->game.pos.side;
    /* The pause between engine moves is for watching; a timed game's
     * clock would pay for it. */
    long since = tc_timed(&state->game.tc) ? 1L << 30 : now_ms() - state->last_move_ms;
    if (!players_should_start(state->players, side, state->paused, state->game.game_over, since))
        return 0;

    char by[48];
    player_label(&state->players[side], by, sizeof(by));
    start_thinking(state, state->drivers[side], by);
    return 0;
}

int handle_command(TUIState *state, const char *cmd) {
    if (!cmd || !cmd[0]) return 1;

    /* Like UCI's "stop": play the best move found so far. */
    if (strcmp(cmd, "stop") == 0) {
        if (!state->thinking) {
            snprintf(state->status, sizeof(state->status), "No search in progress");
            return 1;
        }
        opponent_stop(state->thinking);
        drive_turn(state);
        return 1;
    }
    if (strcmp(cmd, "analyse") == 0 || strncmp(cmd, "analyse ", 8) == 0) {
        const char *name = cmd[7] == ' ' ? cmd + 8 : "";
        if (!name[0]) {
            snprintf(state->status, sizeof(state->status), "Analysis: %s",
                     analysis_wanted(state->analysis_engine) ? state->analysis_engine : "off");
            return 1;
        }
        if (strcmp(name, "off") && strcmp(name, "builtin") && !engines_find(&state->engines, name)) {
            snprintf(state->status, sizeof(state->status), "No engine named %s", name);
            return 1;
        }
        snprintf(state->analysis_engine, sizeof(state->analysis_engine), "%s", name);
        tui_analysis_free(state);
        state->analysis_on = analysis_wanted(name);
        state->analysis_err[0] = '\0';
        snprintf(state->status, sizeof(state->status), "Analysis: %s", name);
        tui_remember_setup(state);
        return 1;
    }
    if (strcmp(cmd, "book") == 0 || strncmp(cmd, "book ", 5) == 0) {
        if (cmd[4] != ' ' || !cmd[5]) {
            snprintf(state->status, sizeof(state->status), "Book: %.240s", state->book_choice);
            return 1;
        }
        snprintf(state->book_choice, sizeof(state->book_choice), "%s", cmd + 5);
        state->cli_book[0] = '\0';   /* a choice made here is remembered */
        snprintf(state->status, sizeof(state->status), "Book: %.240s", state->book_choice);
        cancel_engine_search(state);
        for (int side = WHITE; side <= BLACK; side++) {
            opponent_free(state->drivers[side]);
            state->drivers[side] = NULL;
        }
        opponent_free(state->go_driver);
        state->go_driver = NULL;
        load_book(state);
        tui_attach_players(state);
        tui_remember_setup(state);
        return 1;
    }
    int timed = tc_timed(&state->game.tc);
    if ((strcmp(cmd, "pause") == 0 || strcmp(cmd, "resume") == 0) && !timed &&
        !players_automated(state->players, WHITE) && !players_automated(state->players, BLACK)) {
        snprintf(state->status, sizeof(state->status), "No engine to pause");
        return 1;
    }
    if (strcmp(cmd, "pause") == 0) {
        cancel_engine_search(state);
        set_paused(state, 1);
        if (timed) {
            snprintf(state->status, sizeof(state->status), "Clocks paused — Space or 'resume' to continue");
        } else {
            snprintf(state->status, sizeof(state->status),
                     "Paused — Space or 'resume' to continue, 'go' for one move");
        }
        return 1;
    }
    if (strcmp(cmd, "resume") == 0) {
        set_paused(state, 0);
        state->selected = 0;   /* the engine may move the piece that was picked up */
        memset(state->highlight, 0, sizeof(state->highlight));
        snprintf(state->status, sizeof(state->status), "Resumed");
        return 1;
    }

    /* Moves/"go"/"eval" are rejected while an engine thinks; none of
     * them mean "start over". Everything else is safe to run. */
    int is_move = (cmd[0] >= 'a' && cmd[0] <= 'h' && cmd[1] >= '1' && cmd[1] <= '8');
    int reject_while_thinking =
        is_move ||
        strcmp(cmd, "go") == 0 ||
        strcmp(cmd, "eval") == 0;
    if (timed && state->paused && (is_move || strcmp(cmd, "go") == 0)) {
        snprintf(state->status, sizeof(state->status), "Resume first — the clocks are paused");
        return 1;
    }

    if (reject_while_thinking && state->thinking) {
        snprintf(state->status, sizeof(state->status),
                 "Engine is thinking — please wait, or use 'stop'");
        return 1;
    }

    /* These replace the game state, so a search for the old position is
     * not worth waiting for. */
    if (strcmp(cmd, "new") == 0 ||
        strcmp(cmd, "undo") == 0 || strcmp(cmd, "u") == 0 ||
        strncmp(cmd, "loadfen ", 8) == 0) {
        cancel_engine_search(state);
    }

    if (is_move) {
        if (state->game.game_over) return 1;
        if (!tui_can_move_by_hand(state)) {
            snprintf(state->status, sizeof(state->status),
                     "It is the engine's move — 'pause' to move for it");
            return 1;
        }
        try_move(state, cmd);
        return 1;
    }
    if (strcmp(cmd, "go") == 0) {
        if (state->game.game_over) return 1;
        int side = state->game.pos.side;
        char by[48];
        if (state->drivers[side]) {
            player_label(&state->players[side], by, sizeof(by));
            start_thinking(state, state->drivers[side], by);
        } else {
            Player m = player_builtin(DIFF_MEDIUM);
            player_label(&m, by, sizeof(by));
            start_thinking(state, state->go_driver, by);
        }
        return 1;
    }

    if (strcmp(cmd, "resign") == 0) {
        int side = state->game.pos.side;
        if (players_automated(state->players, side)) side ^= BLACK;
        if (state->game.game_over || players_automated(state->players, side)) {
            snprintf(state->status, sizeof(state->status),
                     state->game.game_over ? "The game is already over" : "No human side to resign");
            return 1;
        }
        cancel_engine_search(state);
        state->game.game_over = 1;
        snprintf(state->game.result, sizeof(state->game.result), "%s resigns — %s wins",
                 side == WHITE ? "White" : "Black", side == WHITE ? "Black" : "White");
        snprintf(state->status, sizeof(state->status), "%s", state->game.result);
        return 1;
    }

    char err[128];
    const char *names[PROFILES_MAX];
    int nn = profiles_names(&state->profiles, names, PROFILES_MAX);
    Player before[2] = { state->players[WHITE], state->players[BLACK] };
    int pc = players_apply_command(state->players, cmd, err, sizeof(err), &state->engines, names, nn);
    if (pc < 0) {
        snprintf(state->status, sizeof(state->status), "%s", err);
        return 1;
    }
    if (pc > 0) {
        state->engine_error[0] = '\0';
        state->selected = 0;
        if (!players_automated(state->players, WHITE) && !players_automated(state->players, BLACK))
            set_paused(state, 0);   /* nothing left to pause */
        memset(state->highlight, 0, sizeof(state->highlight));
        for (int side = WHITE; side <= BLACK; side++)
            if (!same_player(&before[side], &state->players[side]))
                attach(state, side);
        char setup[128];
        describe_setup(state, setup, sizeof(setup));
        snprintf(state->status, sizeof(state->status), "%s", setup);
        return 1;
    }

    if (strcmp(cmd, "engines") == 0) {
        if (!state->engines.count) {
            snprintf(state->status, sizeof(state->status),
                     "No engines registered — add one with e in the start menu");
            return 1;
        }
        char names[200] = "";
        for (int i = 0; i < state->engines.count; i++) {
            if (i) strncat(names, ", ", sizeof(names) - strlen(names) - 1);
            strncat(names, state->engines.e[i].name, sizeof(names) - strlen(names) - 1);
        }
        snprintf(state->status, sizeof(state->status), "Engines: %s", names);
        return 1;
    }
    if (strncmp(cmd, "depth ", 6) == 0) {
        int d = atoi(cmd + 6);
        if (d < 1 || d > 8) {
            snprintf(state->status, sizeof(state->status), "Depth must be 1–8");
            return 1;
        }
        int engines = 0;
        for (int side = WHITE; side <= BLACK; side++) {
            if (state->players[side].kind != PLAYER_BUILTIN) continue;
            state->players[side].depth = d;
            attach(state, side);
            engines++;
        }
        if (engines)
            snprintf(state->status, sizeof(state->status), "Engine depth cap set to %d", d);
        else
            snprintf(state->status, sizeof(state->status),
                     "No engine is playing; depth unchanged");
        return 1;
    }
    if (strcmp(cmd, "undo") == 0 || strcmp(cmd, "u") == 0) {
        tui_undo(state);
        return 1;
    }
    if (strcmp(cmd, "new") == 0) {
        tui_new_game(state);
        return 1;
    }
    if (strcmp(cmd, "pgn") == 0 || strncmp(cmd, "pgn ", 4) == 0) {
        char path[512];
        if (cmd[3] == ' ' && cmd[4])
            snprintf(path, sizeof(path), "%s", cmd + 4);
        else
            pgn_default_path(path, sizeof(path));

        char white[PLAYER_NAME_MAX + 1], black[PLAYER_NAME_MAX + 1];
        players_pgn_name(state->players, WHITE, white, sizeof(white));
        players_pgn_name(state->players, BLACK, black, sizeof(black));
        PgnHeader h = {
            .event = "Casual game",
            .site  = "dchess",
            .white = white,
            .black = black,
        };
        const char *eco = NULL, *opening = book_opening(&state->game, &eco);
        if (opening) {
            h.extra[0][0] = "ECO";     h.extra[0][1] = eco;
            h.extra[1][0] = "Opening"; h.extra[1][1] = opening;
            h.extra_count = 2;
        }

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
        memset(&state->last_search, 0, sizeof(state->last_search));
    state->last_was_book = 0;
        state->last_search_by[0] = '\0';
        snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");
        snprintf(state->status, sizeof(state->status), "Position loaded from FEN");
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
        state->view_side = (state->view_side == WHITE) ? BLACK : WHITE;
        state->selected = 0;
        memset(state->highlight, 0, sizeof(state->highlight));
        snprintf(state->status, sizeof(state->status), "Board turned: %s at the bottom",
                 state->view_side == WHITE ? "White" : "Black");
        return 1;
    }
    if (strcmp(cmd, "eval") == 0) {
        if (analyser_is_builtin(state->analyser)) {   /* one built-in search at a time */
            analyser_stop(state->analyser);
            state->analysis_key = 0;
        }
        SearchResult res = search(&state->game.pos, 1, 0);
        int score_white = eval_white_view(res.best_score, state->game.pos.side);
        snprintf(state->last_eval, sizeof(state->last_eval), "%+.2f", score_white / 100.0f);
        snprintf(state->status, sizeof(state->status), "Eval: %s", state->last_eval);
        return 1;
    }
    if (strcmp(cmd, "stats") == 0) {
        stats_screen(state, 0);
        state->status[0] = '\0';
        if (state->request_redraw) state->request_redraw(state->redraw_ctx);
        return 1;
    }
    if (strcmp(cmd, "help") == 0) {
        snprintf(state->status, sizeof(state->status),
                 "e2e4 go stop pause resume undo new resign swap flip depth N eval fen pgn "
                 "loadfen stats engines book analyse quit | white|black human|engine [level|name]");
        return 1;
    }
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) return -1;

    snprintf(state->status, sizeof(state->status), "Unknown: '%s' (type 'help')", cmd);
    return 1;
}
