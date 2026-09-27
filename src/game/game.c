#include "game/game.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "game/san.h"
#include "engine/hash.h"
#include "engine/fen.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Internals  */

/* Must never stop recording, or threefold detection dies mid-game. */
static void record_position(GameState *g)
{
    g->repetition[g->position_count % GAME_REPETITION_WINDOW] = hash_position(&g->pos);
    g->position_count++;
}

/* Counting itself. Only positions since the last irreversible move can
 * match, so halfmove_clock bounds the scan. */
static int times_repeated(const GameState *g)
{
    if (g->position_count == 0) return 0;

    int window = g->halfmove_clock + 1;   /* includes the current position */
    if (window > g->position_count)        window = g->position_count;
    if (window > GAME_REPETITION_WINDOW)   window = GAME_REPETITION_WINDOW;

    int newest = g->position_count - 1;
    U64 cur = g->repetition[newest % GAME_REPETITION_WINDOW];

    int seen = 0;
    for (int i = 0; i < window; i++)
        if (g->repetition[(newest - i) % GAME_REPETITION_WINDOW] == cur) seen++;

    return seen;
}

static long mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static long (*time_source)(void) = mono_ms;

long game_now_ms(void)                        { return time_source(); }
void game_set_time_source(long (*fn)(void))  { time_source = fn ? fn : mono_ms; }

static long running_ms(const GameState *g)
{
    if (!g->clock_started || g->game_over) return 0;
    long r = (g->clock_paused ? g->paused_at_ms : game_now_ms()) - g->turn_start_ms;
    return r > 0 ? r : 0;
}

static void sync_seconds(GameState *g)
{
    g->white_clock = (int)(g->spent_ms[WHITE] / 1000);
    g->black_clock = (int)(g->spent_ms[BLACK] / 1000);
}

static void start_turn(GameState *g)
{
    g->turn_start_ms = game_now_ms();
    if (g->clock_paused) g->paused_at_ms = g->turn_start_ms;
}

/* Returns the milliseconds charged, for the log. */
static int charge_clock(GameState *g)
{
    long elapsed = running_ms(g);
    g->spent_ms[g->pos.side] += elapsed;
    g->moves_by[g->pos.side]++;
    sync_seconds(g);
    g->clock_started = 1;
    start_turn(g);
    return (int)elapsed;
}

long game_time_spent(const GameState *g, int side)
{
    return g->spent_ms[side] + (side == g->pos.side ? running_ms(g) : 0);
}

long game_time_left(const GameState *g, int side)
{
    return g->tc.base_ms[side] + (long)g->tc.inc_ms[side] * g->moves_by[side]
         - game_time_spent(g, side);
}

void game_clock_pause(GameState *g)
{
    if (g->clock_paused) return;
    g->clock_paused = 1;
    g->paused_at_ms = game_now_ms();
}

void game_clock_resume(GameState *g)
{
    if (!g->clock_paused) return;
    g->turn_start_ms += game_now_ms() - g->paused_at_ms;
    g->clock_paused = 0;
}

static void reset_clocks(GameState *g)
{
    memset(g->spent_ms, 0, sizeof(g->spent_ms));
    memset(g->moves_by, 0, sizeof(g->moves_by));
    g->clock_started = 0;
    g->clock_paused  = 0;
    sync_seconds(g);
}

void game_set_time_control(GameState *g, const TimeControl *tc)
{
    g->tc = *tc;
    reset_clocks(g);
    start_turn(g);
}

/* Must be evaluated BEFORE the move is made. */
static void update_halfmove_clock(GameState *g, Move m, int piece)
{
    int to_sq      = TO(m);
    int is_pawn    = (piece == P || piece == p);
    int is_capture = (to_sq >= 0 && to_sq < 64 &&
                      GET_BIT(g->pos.occupancies[BOTH], to_sq)) ? 1 : 0;

    if (is_pawn || is_capture) g->halfmove_clock = 0;
    else                       g->halfmove_clock++;
}

/* Called before make_move(), which is what san_write() needs. */
static void append_to_log(GameState *g, Move m, int piece, int elapsed)
{
    int i = g->move_count;
    if (i >= MAX_MOVE_HISTORY) return;

    san_write(&g->pos, m, g->move_history[i]);
    g->move_made[i]  = m;
    g->move_piece[i] = piece;
    g->move_time[i]  = elapsed;
    g->move_count++;
}

/* Leaves `pos` alone; callers set the position themselves. */
static void clear_progress(GameState *g, int keep_clocks)
{
    g->move_count      = 0;
    g->halfmove_clock  = 0;
    g->position_count  = 0;
    g->eval_count      = 0;
    g->undo_count      = 0;
    g->game_over         = 0;
    g->result[0]         = '\0';

    /* A timed game always starts fresh: a new position is a new game. */
    if (!keep_clocks || tc_timed(&g->tc)) reset_clocks(g);
    start_turn(g);
}

/* Public API  */

void game_reset(GameState *g)
{
    init_start_position(&g->pos);
    clear_progress(g, 0);
    g->log_start = 0;
    g->start_fen[0] = '\0';
    g->clock_side = g->pos.side;
    record_position(g);
}

int game_load_fen(GameState *g, const char *fen)
{
    Position parsed;
    int hm = 0, fm = 1;

    /* Scratch first, so an invalid FEN cannot touch the live game. */
    if (!parse_fen(fen, &parsed, &hm, &fm)) return 0;

    g->pos = parsed;
    clear_progress(g, 1);
    g->halfmove_clock = hm;
    snprintf(g->start_fen, sizeof(g->start_fen), "%s", fen);

    /* The FEN fullmove number counts move *pairs* from 1, so this is an
     * approximation of half-moves played, used for move numbers. Past
     * move 256 it is capped so the history keeps room to play. */
    if (fm > MAX_MOVE_HISTORY / 4) fm = MAX_MOVE_HISTORY / 4;   /* leave room to play */
    g->move_count = (fm - 1) * 2 + (g->pos.side == BLACK ? 1 : 0);
    g->log_start  = g->move_count;
    g->clock_side = g->pos.side;
    record_position(g);
    return 1;
}

int game_find_move(const GameState *g, int from, int to, int promo, Move *out)
{
    MoveList ml;
    generate_moves(&g->pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;

        if (promo) {
            if (!(FLAGS(m) & promo)) continue;
        } else if (FLAGS(m) & FLAG_PROMOTION) {
            if (!(FLAGS(m) & FLAG_PROMO_Q)) continue;   /* default queen */
        }

        /* generate_moves() is pseudo-legal; this filters illegal ones. */
        Position test = g->pos;
        if (!make_move(&test, m)) return 0;

        if (out) *out = m;
        return 1;
    }
    return 0;
}

void game_play(GameState *g, Move m)
{
    /* Snapshot before anything changes; game_undo() winds back to this. */
    if (g->undo_count < MAX_MOVE_HISTORY) {
        g->undo[g->undo_count].pos            = g->pos;
        g->undo[g->undo_count].halfmove_clock = g->halfmove_clock;
        g->undo_count++;
    }

    int piece   = game_piece_at(g, FROM(m));
    int elapsed = charge_clock(g);

    update_halfmove_clock(g, m, piece);   /* both read the pre-move board */
    append_to_log(g, m, piece, elapsed);

    make_move(&g->pos, m);

    record_position(g);
    g->clock_side = g->pos.side;
}

/* FIDE "dead position": K vs K, K+minor vs K, and K+B vs K+B on the same
 * colour. Two knights and bishop+knight are NOT drawn -- mate is
 * reachable there, which is the test FIDE applies. */
static int insufficient_material(const Position *pos)
{
    if (pos->bitboards[P] | pos->bitboards[p] |
        pos->bitboards[R] | pos->bitboards[r] |
        pos->bitboards[Q] | pos->bitboards[q])
        return 0;

    U64 white_bishops = pos->bitboards[B], black_bishops = pos->bitboards[b];
    int wb = count_bits(white_bishops), bb = count_bits(black_bishops);
    int wn = count_bits(pos->bitboards[N]), bn = count_bits(pos->bitboards[n]);

    int minors = wb + bb + wn + bn;
    if (minors <= 1) return 1;            /* K vs K, or K+minor vs K */

    if (minors == 2 && wb == 1 && bb == 1) {
        int w = lsb(white_bishops), b_ = lsb(black_bishops);
        int w_dark = ((w / 8) + (w % 8)) & 1;
        int b_dark = ((b_ / 8) + (b_ % 8)) & 1;
        return w_dark == b_dark;
    }

    return 0;
}

void game_update_status(GameState *g)
{
    if (g->game_over) return;

    if (!has_legal_moves(&g->pos)) {
        g->game_over = 1;
        if (is_in_check(&g->pos, g->pos.side)) {
            const char *winner = (g->pos.side == WHITE) ? "Black" : "White";
            snprintf(g->result, sizeof(g->result), "Checkmate — %s wins!", winner);
        } else {
            snprintf(g->result, sizeof(g->result), "Stalemate — Draw!");
        }
        return;
    }

    /* After mate (which can happen with un-mating material), before the
     * counting rules that would reach the same verdict 50 moves later. */
    if (insufficient_material(&g->pos)) {
        g->game_over = 1;
        snprintf(g->result, sizeof(g->result), "Insufficient material — Draw!");
        return;
    }

    if (g->halfmove_clock >= 100) {   /* 50 full moves = 100 half-moves */
        g->game_over = 1;
        snprintf(g->result, sizeof(g->result), "Draw by 50-move rule!");
        return;
    }

    if (times_repeated(g) >= 3) {
        g->game_over = 1;
        snprintf(g->result, sizeof(g->result), "Draw by repetition!");
    }
}

/* Recorded just before the engine plays, so it belongs to that move. */
void game_record_eval(GameState *g, int score_cp)
{
    if (g->eval_count >= MAX_MOVE_HISTORY) return;
    g->eval_history[g->eval_count] = score_cp;
    g->eval_ply[g->eval_count]     = g->move_count;
    g->eval_count++;
}

int game_piece_at(const GameState *g, int sq)
{
    if (sq < 0 || sq >= 64) return -1;
    for (int i = 0; i < 12; i++)
        if (GET_BIT(g->pos.bitboards[i], sq)) return i;
    return -1;
}

U64 game_hash(const GameState *g)
{
    return hash_position(&g->pos);
}

int game_can_undo(const GameState *g)
{
    return g->undo_count > 0;
}

int game_undo(GameState *g)
{
    if (!game_can_undo(g)) return 0;

    const UndoRecord *u = &g->undo[--g->undo_count];

    /* Before restoring: the snapshot says whose time to refund. */
    if (g->move_count > 0) {
        int side = u->pos.side;
        g->spent_ms[side] -= g->move_time[g->move_count - 1];
        if (g->spent_ms[side] < 0) g->spent_ms[side] = 0;
        if (g->moves_by[side] > 0) g->moves_by[side]--;
        sync_seconds(g);
    }

    g->pos            = u->pos;
    g->halfmove_clock = u->halfmove_clock;

    if (g->move_count     > 0) g->move_count--;
    if (g->position_count > 0) g->position_count--;
    /* Only engine moves carry an evaluation, so drop just the ones that
     * belonged to the move taken back. */
    while (g->eval_count > 0 && g->eval_ply[g->eval_count - 1] >= g->move_count)
        g->eval_count--;

    /* A takeback resumes a finished game. */
    g->game_over = 0;
    g->result[0] = '\0';

    g->clock_side = g->pos.side;
    start_turn(g);

    return 1;
}

int eval_white_view(int score_cp, int side_to_move)
{
    return side_to_move == WHITE ? score_cp : -score_cp;
}

int game_last_eval(const GameState *g, int *score_cp)
{
    if (g->eval_count <= 0) return 0;
    *score_cp = g->eval_history[g->eval_count - 1];
    return 1;
}

int game_last_move(const GameState *g, Move *m)
{
    if (g->move_count <= g->log_start) return 0;
    *m = g->move_made[g->move_count - 1];
    return 1;
}

/* The winner cannot mate with a lone king or a king and one minor piece. */
static int cannot_mate(const Position *pos, int side)
{
    int o = side == WHITE ? 0 : 6;    /* P..K, then p..k */
    if (pos->bitboards[P + o] | pos->bitboards[R + o] | pos->bitboards[Q + o]) return 0;
    return count_bits(pos->bitboards[N + o]) + count_bits(pos->bitboards[B + o]) <= 1;
}

int game_check_flag(GameState *g)
{
    if (!tc_timed(&g->tc) || g->game_over || !g->clock_started) return 0;
    int side = g->pos.side;
    if (game_time_left(g, side) > 0) return 0;
    /* Stop the clock at exactly zero. */
    g->spent_ms[side] = g->tc.base_ms[side] + (long)g->tc.inc_ms[side] * g->moves_by[side];
    sync_seconds(g);
    g->game_over = 1;
    if (cannot_mate(&g->pos, !side))
        snprintf(g->result, sizeof(g->result), "Time out, insufficient material — Draw!");
    else
        snprintf(g->result, sizeof(g->result), "%s loses on time — %s wins!",
                 side == WHITE ? "White" : "Black", side == WHITE ? "Black" : "White");
    return 1;
}
