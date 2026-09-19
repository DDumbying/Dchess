#include "game/game.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "engine/hash.h"
#include "engine/fen.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ── Internals ──────────────────────────────────────────────────────────── */

/* Append the current position to the repetition ring. Unlike the move
 * log this must never stop recording, or threefold detection quietly
 * dies partway through a long game. */
static void record_position(GameState *g)
{
    g->repetition[g->position_count % GAME_REPETITION_WINDOW] = hash_position(&g->pos);
    g->position_count++;
}

/* How many times the current position has occurred, counting itself.
 * Only positions since the last irreversible move can match, so the
 * scan is bounded by halfmove_clock rather than by the whole game. */
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

/* Charge the side that is about to move for the time it spent thinking,
 * and restart the turn timer. Returns the seconds charged, for the log. */
static int charge_clock(GameState *g)
{
    time_t now = time(NULL);
    int elapsed = 0;

    if (g->clock_started) {
        elapsed = (int)(now - g->turn_start);
        if (g->pos.side == WHITE) g->white_clock += elapsed;
        else                      g->black_clock += elapsed;
    }
    g->clock_started = 1;
    g->turn_start    = now;
    clock_gettime(CLOCK_MONOTONIC, &g->turn_start_mono);

    return elapsed;
}

/* The 50-move counter resets on a pawn move or a capture, and must be
 * evaluated against the board as it stands BEFORE the move is made. */
static void update_halfmove_clock(GameState *g, Move m, int piece)
{
    int to_sq      = TO(m);
    int is_pawn    = (piece == P || piece == p);
    int is_capture = (to_sq >= 0 && to_sq < 64 &&
                      GET_BIT(g->pos.occupancies[BOTH], to_sq)) ? 1 : 0;

    if (is_pawn || is_capture) g->halfmove_clock = 0;
    else                       g->halfmove_clock++;
}

static void append_to_log(GameState *g, Move m, int piece, int elapsed)
{
    int i = g->move_count;
    if (i >= MAX_MOVE_HISTORY) return;

    move_to_str(m, g->move_history[i]);
    g->move_piece[i] = piece;
    g->move_time[i]  = elapsed;
    g->move_count++;
}

/* Clear everything that describes a game in progress, leaving `pos`
 * alone -- callers set the position themselves. */
static void clear_progress(GameState *g, int keep_clocks)
{
    g->move_count      = 0;
    g->halfmove_clock  = 0;
    g->position_count  = 0;
    g->eval_count      = 0;
    g->undo_count      = 0;
    g->game_over         = 0;
    g->result[0]         = '\0';

    if (!keep_clocks) {
        g->white_clock   = 0;
        g->black_clock   = 0;
        g->clock_started = 0;
    }
    g->turn_start = time(NULL);
    clock_gettime(CLOCK_MONOTONIC, &g->turn_start_mono);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void game_reset(GameState *g)
{
    init_start_position(&g->pos);
    clear_progress(g, 0);
    g->clock_side = g->pos.side;
    record_position(g);
}

int game_load_fen(GameState *g, const char *fen)
{
    Position parsed;
    int hm = 0, fm = 1;

    /* Parse into a scratch position first: parse_fen() is documented to
     * leave its output untouched on failure, but keeping the live game
     * out of it entirely makes "invalid FEN changes nothing" true here
     * regardless of what the parser does. */
    if (!parse_fen(fen, &parsed, &hm, &fm)) return 0;

    g->pos = parsed;
    clear_progress(g, 1);
    g->halfmove_clock = hm;

    /* The FEN fullmove number counts move *pairs* from 1, so this is an
     * approximation of half-moves played, used only for display. */
    g->move_count = (fm - 1) * 2 + (g->pos.side == BLACK ? 1 : 0);
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
            /* No promotion piece asked for: default to a queen rather
             * than whichever underpromotion happens to come first. */
            if (!(FLAGS(m) & FLAG_PROMO_Q)) continue;
        }

        /* generate_moves() is pseudo-legal, so this is the step that
         * rules out moves leaving the mover's own king in check. */
        Position test = g->pos;
        if (!make_move(&test, m)) return 0;

        if (out) *out = m;
        return 1;
    }
    return 0;
}

void game_play(GameState *g, Move m)
{
    /* Snapshot BEFORE anything changes -- this is what game_undo() winds
     * back to. Taken first so it cannot accidentally capture a
     * half-applied state. */
    if (g->undo_count < MAX_MOVE_HISTORY) {
        g->undo[g->undo_count].pos            = g->pos;
        g->undo[g->undo_count].halfmove_clock = g->halfmove_clock;
        g->undo_count++;
    }

    int piece   = game_piece_at(g, FROM(m));
    int elapsed = charge_clock(g);

    update_halfmove_clock(g, m, piece);   /* must precede make_move() */
    append_to_log(g, m, piece, elapsed);  /* ditto: reads the pre-move board */

    make_move(&g->pos, m);

    record_position(g);
    g->clock_side = g->pos.side;
}

/* Is checkmate still possible for either side with the material left on
 * the board? FIDE calls a position where it is not a "dead position",
 * and the game is drawn immediately.
 *
 * The drawn cases are K vs K, K plus a single minor vs K, and K+B vs
 * K+B with both bishops on the same colour (neither can ever attack the
 * other's squares, so the position can never be forced). Everything
 * else is treated as sufficient: a pawn, rook or queen can obviously
 * mate, and so can two knights or bishop+knight -- for two knights the
 * mate is not forced, but it is reachable, which is the test FIDE
 * applies here. */
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

    /* Checked after mate/stalemate (a position can be mate with material
     * that could never mate again) but before the counting rules, which
     * would otherwise take another 50 moves to reach the same verdict. */
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

void game_record_eval(GameState *g, int score_cp)
{
    if (g->eval_count < MAX_MOVE_HISTORY)
        g->eval_history[g->eval_count++] = score_cp;
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

    /* Refund the clock before restoring the position: which side gets
     * the time back is decided by whose turn it was when the move was
     * played, which is the side to move in the snapshot. */
    if (g->move_count > 0) {
        int spent = g->move_time[g->move_count - 1];
        if (u->pos.side == WHITE) g->white_clock -= spent;
        else                      g->black_clock -= spent;
        if (g->white_clock < 0) g->white_clock = 0;
        if (g->black_clock < 0) g->black_clock = 0;
    }

    g->pos            = u->pos;
    g->halfmove_clock = u->halfmove_clock;

    if (g->move_count     > 0) g->move_count--;
    if (g->position_count > 0) g->position_count--;
    if (g->eval_count     > 0) g->eval_count--;

    /* Taking a move back resumes a finished game -- otherwise the board
     * would be playable again while still flagged as over. */
    g->game_over = 0;
    g->result[0] = '\0';

    g->clock_side = g->pos.side;
    g->turn_start = time(NULL);
    clock_gettime(CLOCK_MONOTONIC, &g->turn_start_mono);

    return 1;
}
