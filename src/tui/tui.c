#include "tui/tui.h"
#include "tui/render.h"
#include "tui/input.h"
#include "tui/commands.h"
#include "tui/stats_tui.h"
#include "engine/board.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include "utils/stats.h"
#include "utils/cli.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define CP_CANVAS    35
#define CP_BORDER    21
#define CP_TITLE     22
#define CP_HINT      29
#define CP_INFO_VAL  26
#define CP_STATUS_OK 27
#define CP_STATUS_ERR 28
#define CP_SEL_PC    10

/* ── Simple position hash for repetition detection ──────────────────────── */
static U64 hash_position(const Position *pos)
{
    U64 h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 12; i++) {
        U64 bb = pos->bitboards[i];
        h ^= bb * 0x9e3779b97f4a7c15ULL;
        h += (h << 6) + (h >> 2);
    }
    h ^= (U64)pos->side    * 0x517cc1b727220a95ULL;
    h ^= (U64)pos->castling * 0x6c62272e07bb0142ULL;
    h ^= (U64)(pos->enpassant + 1) * 0xbf58476d1ce4e5b9ULL;
    return h;
}

/* ── Check 50-move rule and 3-fold repetition ───────────────────────────── */
static void check_draw_rules(TUIState *state)
{
    if (state->game_over) return;

    /* 50-move rule (halfmove_clock tracks moves since pawn move/capture) */
    if (state->halfmove_clock >= 100) {  /* 50 moves = 100 half-moves */
        state->game_over = 1;
        snprintf(state->game_result, sizeof(state->game_result),
                 "Draw by 50-move rule!");
        return;
    }

    /* 3-fold repetition */
    U64 cur = hash_position(&state->pos);
    int count = 0;
    for (int i = 0; i < state->pos_history_count; i++)
        if (state->pos_history[i] == cur) count++;
    if (count >= 2) {   /* current + 2 previous = 3-fold */
        state->game_over = 1;
        snprintf(state->game_result, sizeof(state->game_result),
                 "Draw by repetition!");
        return;
    }
}

/* ── Record position hash after a move ──────────────────────────────────── */
static void record_position(TUIState *state)
{
    if (state->pos_history_count < MAX_MOVE_HISTORY)
        state->pos_history[state->pos_history_count++] = hash_position(&state->pos);
}

/* ── Update halfmove clock ───────────────────────────────────────────────── */
static void update_halfmove(TUIState *state, int from_sq, int to_sq, int piece)
{
    int is_pawn    = (piece == 0 || piece == 6);
    int is_capture = GET_BIT(state->pos.occupancies[BOTH], to_sq);  /* before move */
    (void)from_sq;
    if (is_pawn || is_capture)
        state->halfmove_clock = 0;
    else
        state->halfmove_clock++;
}

/* ── Check no legal moves (checkmate / stalemate) ───────────────────────── */
static void check_game_over(TUIState *state)
{
    if (state->game_over) return;

    MoveList ml;
    generate_moves(&state->pos, &ml);
    int legal = 0;
    for (int i = 0; i < ml.count; i++) {
        Position tmp;
        memcpy(&tmp, &state->pos, sizeof(Position));
        if (make_move(&tmp, ml.moves[i])) { legal = 1; break; }
    }

    if (!legal) {
        state->game_over = 1;
        if (is_in_check(&state->pos, state->pos.side)) {
            const char *w = state->pos.side == WHITE ? "Black" : "White";
            snprintf(state->game_result, sizeof(state->game_result),
                     "Checkmate — %s wins!", w);
        } else {
            snprintf(state->game_result, sizeof(state->game_result),
                     "Stalemate — Draw!");
        }
        return;
    }

    check_draw_rules(state);
}

/* ── Game-over popup ────────────────────────────────────────────────────── */
static void show_game_over_popup(WINDOW *board_win, TUIState *state)
{
    int bh, bw;
    getmaxyx(board_win, bh, bw);

    int pw = 46, ph = 9;
    int pr = (bh - ph) / 2;
    int pc_col = (bw - pw) / 2;

    WINDOW *pop = newwin(ph, pw, pr, pc_col);
    keypad(pop, TRUE);

    wattron(pop, COLOR_PAIR(CP_BORDER));
    box(pop, ACS_VLINE, ACS_HLINE);
    wattroff(pop, COLOR_PAIR(CP_BORDER));

    wattron(pop, COLOR_PAIR(CP_TITLE)|A_BOLD);
    mvwprintw(pop, 0, (pw-11)/2, " GAME OVER ");
    wattroff(pop, COLOR_PAIR(CP_TITLE)|A_BOLD);

    wattron(pop, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);
    int rlen = (int)strlen(state->game_result);
    mvwprintw(pop, 2, (pw - rlen) / 2, "%s", state->game_result);
    wattroff(pop, COLOR_PAIR(CP_INFO_VAL)|A_BOLD);

    int total_moves = (state->move_count + 1) / 2;
    int ws = state->white_clock, bs = state->black_clock;
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

    /* ── Save stats for this completed game ───────────────────────────── */
    {
        int result = 0; /* draw by default */
        const char *r = state->game_result;
        /* Check if human won or lost */
        if (strstr(r, "White wins")) {
            result = (state->player_side == WHITE) ? 1 : -1;
        } else if (strstr(r, "Black wins")) {
            result = (state->player_side == BLACK) ? 1 : -1;
        }
        int total_secs = state->white_clock + state->black_clock;
        stats_record(&state->stats,
                     state->difficulty,
                     result,
                     state->player_side,
                     state->move_count,
                     total_secs);
        stats_save(&state->stats);
    }

    while (1) {
        int ch = wgetch(pop);
        if (ch == 'r' || ch == 'R') {
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
            state->view_side         = WHITE;
            memset(state->highlight, 0, sizeof(state->highlight));

            const char *diff_str = (state->difficulty == DIFF_EASY)   ? "Easy"   :
                                   (state->difficulty == DIFF_HARD)   ? "Hard"   : "Medium";
            const char *side_str = (state->player_side == WHITE) ? "White" : "Black";
            snprintf(state->status, sizeof(state->status),
                     "New game – You play %s | %s difficulty", side_str, diff_str);
            break;
        }
        if (ch == 'q' || ch == 'Q') {
            delwin(pop);
            tui_cleanup();
            exit(0);
        }
    }
    delwin(pop);
}

/* ── Legal move highlights ──────────────────────────────────────────────── */
static void build_highlights(TUIState *state)
{
    memset(state->highlight, 0, sizeof(state->highlight));
    if (!state->selected) return;

    int flipped = (state->view_side == BLACK);
    int from_rank = flipped ? state->sel_row : (7 - state->sel_row);
    int from_file = flipped ? (7 - state->sel_col) : state->sel_col;
    int from = from_rank * 8 + from_file;

    MoveList ml;
    generate_moves(&state->pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from) continue;
        Position tmp;
        memcpy(&tmp, &state->pos, sizeof(Position));
        if (!make_move(&tmp, m)) continue;
        int to = TO(m);
        int to_rank = to / 8;
        int to_file = to % 8;
        /* Convert board square back to screen row/col */
        int srow = flipped ? to_rank : (7 - to_rank);
        int scol = flipped ? (7 - to_file) : to_file;
        state->highlight[srow][scol] = 1;
    }
}

/* ── Commit a move: update clocks, halfmove, history ───────────────────── */
static void commit_move(TUIState *state, Move m, int piece, const char *buf)
{
    int to_sq = TO(m);

    /* Clock: charge the side that just moved */
    time_t now = time(NULL);
    int elapsed = 0;
    if (state->clock_started) {
        elapsed = (int)(now - state->turn_start);
        if (state->pos.side == WHITE)
            state->white_clock += elapsed;
        else
            state->black_clock += elapsed;
    }
    state->clock_started = 1;
    state->turn_start = now;
    clock_gettime(CLOCK_MONOTONIC, &state->turn_start_mono);

    /* Halfmove clock — check BEFORE moving */
    int is_pawn    = (piece == 0 || piece == 6);
    int is_capture = 0;
    if (to_sq >= 0 && to_sq < 64)
        is_capture = GET_BIT(state->pos.occupancies[BOTH], to_sq) ? 1 : 0;
    if (is_pawn || is_capture) state->halfmove_clock = 0;
    else                       state->halfmove_clock++;

    /* Make the move */
    make_move(&state->pos, m);

    /* Record position hash */
    record_position(state);

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

/* ── Cursor Enter ───────────────────────────────────────────────────────── */
static void cursor_enter(TUIState *state, WINDOW *board_win)
{
    int flipped = (state->view_side == BLACK);
    int rank = flipped ? state->cursor_row : (7 - state->cursor_row);
    int file = flipped ? (7 - state->cursor_col) : state->cursor_col;
    int sq   = rank * 8 + file;

    if (!state->selected) {
        int piece = -1;
        for (int i = 0; i < 12; i++)
            if (GET_BIT(state->pos.bitboards[i], sq)) { piece = i; break; }

        int friendly = (piece >= 0) &&
                       ((state->pos.side == WHITE && piece < 6) ||
                        (state->pos.side == BLACK && piece >= 6));
        if (!friendly) {
            snprintf(state->status, sizeof(state->status),
                     "No friendly piece on %c%d", 'a'+file, rank+1);
            return;
        }
        state->selected = 1;
        state->sel_row  = state->cursor_row;
        state->sel_col  = state->cursor_col;
        build_highlights(state);
        snprintf(state->status, sizeof(state->status),
                 "Selected %c%d — move cursor to destination and press Enter",
                 'a'+file, rank+1);
    } else {
        if (state->cursor_row == state->sel_row &&
            state->cursor_col == state->sel_col) {
            state->selected = 0;
            memset(state->highlight, 0, sizeof(state->highlight));
            snprintf(state->status, sizeof(state->status), "Deselected.");
            return;
        }

        int from_rank_s = flipped ? state->sel_row : (7 - state->sel_row);
        int from_file_s = flipped ? (7 - state->sel_col) : state->sel_col;
        int from_sq = from_rank_s * 8 + from_file_s;
        int to_sq   = sq;

        MoveList ml;
        generate_moves(&state->pos, &ml);
        int moved = 0;

        for (int i = 0; i < ml.count; i++) {
            Move mv = ml.moves[i];
            if (FROM(mv) != from_sq || TO(mv) != to_sq) continue;
            if ((FLAGS(mv) & FLAG_PROMOTION) && !(FLAGS(mv) & FLAG_PROMO_Q)) continue;

            int piece = -1;
            for (int j = 0; j < 12; j++)
                if (GET_BIT(state->pos.bitboards[j], from_sq)) { piece = j; break; }

            Position tmp;
            memcpy(&tmp, &state->pos, sizeof(Position));
            char buf[8];
            move_to_str(mv, buf);

            if (!make_move(&tmp, mv)) {
                snprintf(state->status, sizeof(state->status),
                         "Illegal: leaves king in check");
                break;
            }
            commit_move(state, mv, piece, buf);
            snprintf(state->status, sizeof(state->status), "Played: %s", buf);

            check_game_over(state);
            state->selected = 0;
            memset(state->highlight, 0, sizeof(state->highlight));

            if (state->game_over) {
                return;
            }
            state->clock_side = state->pos.side;

            if (state->two_player) {
                state->view_side  = state->pos.side;
                state->cursor_row = 6;
                state->cursor_col = 4;
            } else if (state->engine_side == state->pos.side) {
                handle_command(state, "go");
                state->clock_side = state->pos.side;
                check_game_over(state);
            }

            moved = 1;
            break;
        }

        if (!moved) {
            if (!state->highlight[state->cursor_row][state->cursor_col])
                snprintf(state->status, sizeof(state->status),
                         "Not a legal move — select a highlighted square.");
            state->selected = 0;
            memset(state->highlight, 0, sizeof(state->highlight));
        }
    }
}

void tui_init(TUIState *state, const CliArgs *args)
{
    memset(state, 0, sizeof(*state));
    init_start_position(&state->pos);

    /* Apply CLI configuration */
    state->player_side  = args ? args->player_side  : WHITE;
    state->difficulty   = args ? args->difficulty   : DIFF_MEDIUM;
    state->engine_depth = args ? args->engine_depth : 5;
    state->two_player   = args ? args->two_player   : 0;

    /* Engine plays the opposite side of the human (disabled in two-player) */
    state->engine_side  = state->two_player ? -1 :
                          (state->player_side == WHITE) ? BLACK : WHITE;

    /* Board is always viewed from white's side initially */
    state->view_side    = WHITE;

    /* If player chose black, engine goes first – queue it */
    state->cursor_row   = 6;
    state->cursor_col   = 4;
    state->turn_start   = time(NULL);
    clock_gettime(CLOCK_MONOTONIC, &state->turn_start_mono);

    /* Load persistent stats */
    stats_load(&state->stats);

    record_position(state);   /* record starting position */
    snprintf(state->last_eval, sizeof(state->last_eval), "+0.00");

    const char *diff_str = (state->difficulty == DIFF_EASY)   ? "Easy"   :
                           (state->difficulty == DIFF_HARD)   ? "Hard"   : "Medium";
    const char *side_str = (state->player_side == WHITE) ? "White" : "Black";
    snprintf(state->status, sizeof(state->status),
             "Ready – You play %s | %s difficulty | arrows/hjkl=move  enter=select",
             side_str, diff_str);
}

void tui_cleanup(void) { endwin(); }

void tui_run(TUIState *state)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    init_colors();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int cmd_h   = 3;
    int main_h  = rows - cmd_h;
    /* Layout: [INFO panel] [EVAL BAR] [BOARD]
     * eval_bar is 3 cols wide — narrow vertical strip beside the board */
    int info_w    = (cols >= 90) ? 32 : (cols >= 70 ? 26 : (cols >= 55 ? 20 : 0));
    int eval_bar_w = (cols >= 55) ? 3 : 0;
    int board_w   = cols - info_w - eval_bar_w;

    WINDOW *info_win     = info_w      ? newwin(main_h, info_w,      0, 0)                   : NULL;
    WINDOW *eval_bar_win = eval_bar_w  ? newwin(main_h, eval_bar_w,  0, info_w)              : NULL;
    WINDOW *board_win    =               newwin(main_h, board_w,     0, info_w + eval_bar_w);
    WINDOW *cmd_win      =               newwin(cmd_h,  cols,    main_h, 0);

    /* Stats overlay window removed — draw_stats_mini creates its own popup */

    wbkgd(stdscr, COLOR_PAIR(CP_CANVAS));
    werase(stdscr);
    wrefresh(stdscr);

    keypad(board_win, TRUE);
    keypad(cmd_win,   TRUE);

    /* Redraw every 100 ms so clocks tick live without waiting for input */
    wtimeout(cmd_win, 100);

    char cmd_buf[64];

    /* If player chose Black, engine (White) goes first */
    if (state->engine_side == WHITE) {
        handle_command(state, "go");
    }

    /* clock_side tracks whose clock is ticking; starts as the side to move */
    state->clock_side = state->pos.side;

    while (1) {

        /* -- If game just ended (e.g. engine delivered checkmate),
         *    show the popup immediately before the next render -- */
        if (state->game_over && state->game_result[0]) {
            /* Render the final board state first so it's visible behind popup */
            werase(stdscr); wnoutrefresh(stdscr);
            render_all(board_win, info_win, eval_bar_win, cmd_win, state);
            touchwin(stdscr); touchwin(board_win);
            if (info_win)     touchwin(info_win);
            if (eval_bar_win) touchwin(eval_bar_win);
            touchwin(cmd_win);
            wnoutrefresh(stdscr); wnoutrefresh(board_win);
            if (info_win)     wnoutrefresh(info_win);
            if (eval_bar_win) wnoutrefresh(eval_bar_win);
            wnoutrefresh(cmd_win);
            doupdate();
            show_game_over_popup(board_win, state);
            /* game_result is cleared by popup on new game; clear flag too */
            state->game_result[0] = '\0';
            state->clock_side = state->pos.side;
        }

        /* ── Normal game rendering ──────────────────────────────────── */
        werase(stdscr);
        wnoutrefresh(stdscr);
        render_all(board_win, info_win, eval_bar_win, cmd_win, state);
        touchwin(stdscr);
        touchwin(board_win);
        if (info_win)     touchwin(info_win);
        if (eval_bar_win) touchwin(eval_bar_win);
        touchwin(cmd_win);
        wnoutrefresh(stdscr);
        wnoutrefresh(board_win);
        if (info_win)     wnoutrefresh(info_win);
        if (eval_bar_win) wnoutrefresh(eval_bar_win);
        wnoutrefresh(cmd_win);
        doupdate();

        int ch = read_key(cmd_win, cmd_buf, sizeof(cmd_buf), &state->insert_mode);

        /* Tab key → show small stats popup centered over the board */
        if (ch == '\t') {
            stats_load(&state->stats);
            draw_stats_mini(board_win, &state->stats);
            /* Repaint normal game underneath before looping */
            werase(stdscr);
            wnoutrefresh(stdscr);
            touchwin(board_win);
            if (info_win)     touchwin(info_win);
            if (eval_bar_win) touchwin(eval_bar_win);
            touchwin(cmd_win);
            continue;
        }

        switch (ch) {
            case KEY_UP:    case 'k':
                if (state->cursor_row > 0) state->cursor_row--; break;
            case KEY_DOWN:  case 'j':
                if (state->cursor_row < 7) state->cursor_row++; break;
            case KEY_LEFT:  case 'h':
                if (state->cursor_col > 0) state->cursor_col--; break;
            case KEY_RIGHT: case 'l':
                if (state->cursor_col < 7) state->cursor_col++; break;
            case '\n': case '\r': case KEY_ENTER:
                if (!state->game_over) cursor_enter(state, board_win);
                break;
            case 27:
                state->selected = 0;
                memset(state->highlight, 0, sizeof(state->highlight));
                snprintf(state->status, sizeof(state->status), "Deselected.");
                break;
            case -2:
                if (cmd_buf[0]) {
                    int ret = handle_command(state, cmd_buf);
                    if (ret == -1) goto quit;
                    state->clock_side = state->pos.side;
                    check_game_over(state);
                    /* game_over is caught at top of next loop iteration */
                }
                break;
            default: break;
        }
    }
quit:
    delwin(board_win);
    if (eval_bar_win) delwin(eval_bar_win);
    if (info_win) delwin(info_win);
    delwin(cmd_win);
    tui_cleanup();
}
